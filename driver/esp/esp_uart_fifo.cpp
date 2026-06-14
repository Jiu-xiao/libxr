#include <algorithm>

#include "esp_attr.h"
#include "esp_uart.hpp"
#include "soc/uart_periph.h"

namespace
{
// FIFO RX interrupt reasons handled by the non-DMA path.
// 非 DMA 路径处理的 FIFO RX 中断原因。
constexpr uint32_t UART_RX_INTR_MASK =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;

// FIFO TX interrupt reason handled by the non-DMA path.
// 非 DMA 路径处理的 FIFO TX 中断原因。
constexpr uint32_t UART_TX_INTR_MASK = UART_INTR_TXFIFO_EMPTY;
}  // namespace

namespace LibXR
{

// ISR entry only forwards control into the object instance.
// ISR 入口只负责把控制转发给对象实例。
void IRAM_ATTR ESP32UART::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UART*>(arg);
  if (self != nullptr)
  {
    self->HandleUartInterrupt();
  }
}

// FIFO mode relies on a UART peripheral interrupt instead of the UHCI/GDMA
// backend, and some classic ESP targets cannot safely keep this interrupt in
// IRAM because the write-side queue path still touches flash-resident code.
// FIFO 模式依赖 UART 外设中断而不是 UHCI/GDMA 后端；同时某些经典 ESP 目标
// 由于写侧队列路径仍会触碰 flash 代码，因此不能安全地把该中断放入 IRAM。
ErrorCode ESP32UART::InstallUartIsr()
{
  if (uart_isr_installed_)
  {
    return ErrorCode::OK;
  }

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
  // Classic ESP32/S3 can enter cache-disabled flash windows while UART IRQ is active.
  // The current WritePort path is not fully IRAM-safe, so keep this IRQ non-IRAM.
  constexpr int UART_INTR_FLAGS = 0;
#else
  constexpr int UART_INTR_FLAGS = ESP_INTR_FLAG_IRAM;
#endif

  const esp_err_t err = esp_intr_alloc(uart_periph_signal[uart_num_].irq, UART_INTR_FLAGS,
                                       UartIsrEntry, this, &uart_intr_handle_);
  if (err != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

// FIFO TX path streams bytes directly from the queue into the hardware FIFO.
// Unlike DMA mode, it does not stage a second payload block.
// FIFO TX 路径会直接把字节从队列流式写入硬件 FIFO；与 DMA 模式不同，它不
// 会暂存第二块 payload。
void IRAM_ATTR ESP32UART::FillTxFifo(bool in_isr)
{
  if (!tx_active_valid_)
  {
    return;
  }

  while (tx_active_offset_ < tx_active_length_)
  {
    const uint32_t fifo_space = uart_hal_get_txfifo_len(&uart_hal_);
    if (fifo_space == 0U)
    {
      break;
    }

    const size_t remaining = tx_active_length_ - tx_active_offset_;
    const size_t chunk_size = std::min<size_t>(remaining, fifo_space);

    const ErrorCode pop_ec = write_port_->queue_data_->PopWithReader(
        chunk_size,
        [this](const uint8_t* src, size_t size) -> ErrorCode
        {
          uint32_t write_size = 0;
          uart_hal_write_txfifo(&uart_hal_, src, static_cast<uint32_t>(size),
                                &write_size);
          return (write_size == static_cast<uint32_t>(size)) ? ErrorCode::OK
                                                             : ErrorCode::EMPTY;
        });
    if (pop_ec != ErrorCode::OK)
    {
      ASSERT(false);
      break;
    }

    tx_active_offset_ += chunk_size;
  }

  if ((tx_active_offset_ < tx_active_length_) || !in_isr)
  {
    return;
  }

  uart_hal_disable_intr_mask(&uart_hal_, UART_INTR_TXFIFO_EMPTY);
  OnTxTransferDone(true, ErrorCode::OK);
}

// RX FIFO draining is best-effort: read as much as the software queue can
// absorb, then stop without resetting the whole hardware FIFO.
// RX FIFO 清空采用尽力而为策略：尽量读取软件队列还能容纳的部分，然后停止，
// 不会粗暴重置整个硬件 FIFO。
void IRAM_ATTR ESP32UART::DrainRxFifo(bool in_isr)
{
  if (rx_fifo_draining_.TestAndSet())
  {
    return;
  }

  bool pushed_any = false;
  while (uart_hal_get_rxfifo_len(&uart_hal_) > 0U)
  {
    const size_t fifo_len = uart_hal_get_rxfifo_len(&uart_hal_);
    const size_t write_len = std::min(fifo_len, read_port_->queue_data_->EmptySize());
    if (write_len == 0U)
    {
      break;
    }

    const ErrorCode push_ec = read_port_->queue_data_->PushWithWriter(
        write_len,
        [this](uint8_t* buffer, size_t chunk_size) -> ErrorCode
        {
          int read_len = static_cast<int>(chunk_size);
          uart_hal_read_rxfifo(&uart_hal_, buffer, &read_len);
          return (read_len == static_cast<int>(chunk_size)) ? ErrorCode::OK
                                                            : ErrorCode::EMPTY;
        });

    if ((push_ec == ErrorCode::FULL) || (push_ec == ErrorCode::EMPTY))
    {
      break;
    }
    if (push_ec != ErrorCode::OK)
    {
      ASSERT(false);
      break;
    }

    pushed_any = true;
  }

  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }

  rx_fifo_draining_.Clear();
}

// RX interrupt handling distinguishes overflow from ordinary data-ready events
// so that overflow can be acknowledged without discarding remaining FIFO bytes.
// RX 中断处理区分 overflow 和普通 data-ready 事件，以便在确认 overflow
// 的同时保留剩余 FIFO 字节。
void IRAM_ATTR ESP32UART::HandleRxInterrupt(uint32_t uart_intr_status)
{
  const bool has_overflow = (uart_intr_status & UART_INTR_RXFIFO_OVF) != 0U;
  const bool has_rx_data =
      (uart_intr_status & (UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT)) != 0U;

  if (!has_overflow && !has_rx_data)
  {
    return;
  }

  if (has_overflow)
  {
    // Overrun means at least one incoming byte was dropped. Keep remaining FIFO
    // bytes to minimize extra loss instead of resetting the whole RX FIFO.
    uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_OVF);
  }

  DrainRxFifo(true);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
}

// TX interrupt handling enters the FIFO refill path under a scoped reentry
// guard so that queue callbacks cannot recursively restart TX.
// TX 中断处理在受控的重入保护下进入 FIFO 补料路径，避免队列回调递归重启 TX。
void IRAM_ATTR ESP32UART::HandleTxInterrupt(uint32_t uart_intr_status)
{
  if ((uart_intr_status & UART_INTR_TXFIFO_EMPTY) == 0U)
  {
    return;
  }

  Flag::ScopedRestore tx_flag(in_tx_isr_);
  FillTxFifo(true);
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_TXFIFO_EMPTY);
}

// The FIFO backend drains all currently pending UART interrupt reasons before
// leaving the ISR to avoid orphaning a same-cycle RX/TX event.
// FIFO 后端会在离开 ISR 前排空当前所有待处理 UART 中断原因，避免同周期的
// RX/TX 事件被遗留。
void IRAM_ATTR ESP32UART::HandleUartInterrupt()
{
  uint32_t uart_intr_status = uart_hal_get_intsts_mask(&uart_hal_);

  while (uart_intr_status != 0)
  {
    if (uart_intr_status & UART_RX_INTR_MASK)
    {
      HandleRxInterrupt(uart_intr_status);
    }

    if (uart_intr_status & UART_TX_INTR_MASK)
    {
      HandleTxInterrupt(uart_intr_status);
    }

    uart_intr_status = uart_hal_get_intsts_mask(&uart_hal_);
  }
}

}  // namespace LibXR
