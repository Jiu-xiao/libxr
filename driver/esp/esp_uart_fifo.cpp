#include <algorithm>

#include "esp_attr.h"
#include "esp_uart.hpp"
#include "soc/uart_periph.h"

namespace
{
constexpr uint32_t UART_RX_INTR_MASK =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;
constexpr uint32_t UART_TX_INTR_MASK = UART_INTR_TXFIFO_EMPTY;
}  // namespace

namespace LibXR
{

void IRAM_ATTR ESP32UART::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UART*>(arg);
  if (self != nullptr)
  {
    self->HandleUartInterrupt();
  }
}

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

void IRAM_ATTR ESP32UART::FillTxFifo(bool in_isr)
{
  if (!tx_active_valid_)
  {
    return;
  }

#if SOC_GDMA_SUPPORTED && SOC_UHCI_SUPPORTED
  if (dma_backend_enabled_)
  {
    while (tx_active_offset_ < tx_active_length_)
    {
      if (uart_hal_get_txfifo_len(&uart_hal_) == 0)
      {
        break;
      }

      uint32_t write_size = 0;
      const uint8_t* src = tx_active_buffer_ + tx_active_offset_;
      const uint32_t remaining =
          static_cast<uint32_t>(tx_active_length_ - tx_active_offset_);

      uart_hal_write_txfifo(&uart_hal_, src, remaining, &write_size);
      if (write_size == 0)
      {
        break;
      }

      tx_active_offset_ += write_size;
    }
  }
  else
#endif
  {
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
  }

  if ((tx_active_offset_ < tx_active_length_) || !in_isr)
  {
    return;
  }

  uart_hal_disable_intr_mask(&uart_hal_, UART_INTR_TXFIFO_EMPTY);
  OnTxTransferDone(true, ErrorCode::OK);
}

void IRAM_ATTR ESP32UART::DrainRxFifoFromIsr()
{
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
    read_port_->ProcessPendingReads(true);
  }
}

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

  DrainRxFifoFromIsr();
  uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
}

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
