#include <algorithm>

#include "esp_attr.h"
#include "esp_uart.hpp"
#include "soc/uart_periph.h"

namespace
{
constexpr uint32_t UART_RX_DATA_INTR_MASK = UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT;
constexpr uint32_t UART_RX_INTR_MASK = UART_RX_DATA_INTR_MASK | UART_INTR_RXFIFO_OVF;
constexpr uint32_t UART_TX_INTR_MASK = UART_INTR_TXFIFO_EMPTY;
}  // namespace

namespace LibXR
{

void IRAM_ATTR ESP32UART::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UART*>(arg);
  if (self == nullptr)
  {
    return;
  }

#if LIBXR_ESP_UART_HAS_AHB_GDMA
  if (self->dma_backend_enabled_)
  {
    (void)self->execution_policy_.InvokeIrq(
        [self]() noexcept { return self->ServiceDmaUartStatus(true); },
        [self](uint32_t events) noexcept
        { return self->tx_dma_model_.Service(events, true); });
    return;
  }
#endif

  bool pushed_any = false;
  (void)self->execution_policy_.InvokeIrq(
      [self]() noexcept { return self->ServiceFifoIrqSource(true); },
      [self, &pushed_any](uint32_t events) noexcept
      { return self->ServiceFifo(events, true, nullptr, pushed_any); });
  if (pushed_any)
  {
    self->read_port_->ProcessPendingReads(true);
  }
}

ErrorCode ESP32UART::InstallUartIsr()
{
  if (uart_isr_installed_)
  {
    return ErrorCode::OK;
  }

  constexpr int UART_INTR_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;

  const esp_err_t err = esp_intr_alloc(uart_periph_signal[uart_num_].irq, UART_INTR_FLAGS,
                                       UartIsrEntry, this, &uart_intr_handle_);
  if (err != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

ErrorCode IRAM_ATTR ESP32UART::SubmitFifoTx(bool in_isr)
{
  FifoSubmitContext submit{};
  bool pushed_any = false;
  (void)execution_policy_.Invoke(
      FIFO_EVENT_WRITE, [this, in_isr, &submit, &pushed_any](uint32_t events) noexcept
      { return ServiceFifo(events, in_isr, &submit, pushed_any); });
  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }
  return submit.result;
}

uint32_t IRAM_ATTR ESP32UART::ServiceFifo(uint32_t events, bool in_isr,
                                          FifoSubmitContext* submit,
                                          bool& pushed_any) noexcept
{
  if ((events & FIFO_EVENT_RX_DRAIN) != 0U)
  {
    pushed_any = DrainRxFifo(in_isr) || pushed_any;
    uart_hal_clr_intsts_mask(&uart_hal_, UART_RX_DATA_INTR_MASK);
    if (read_port_->queue_data_->EmptySize() == 0U)
    {
      uart_hal_disable_intr_mask(&uart_hal_, UART_RX_DATA_INTR_MASK);
    }
    else
    {
      uart_hal_ena_intr_mask(&uart_hal_, UART_RX_DATA_INTR_MASK);
    }
  }

  bool progress = (events & FIFO_EVENT_WRITE) != 0U;
  if ((events & FIFO_EVENT_IRQ) != 0U)
  {
    if (tx_active_valid_)
    {
      FillTxFifo(in_isr);
      if (tx_active_offset_ == tx_active_length_)
      {
        uart_hal_disable_intr_mask(&uart_hal_, UART_TX_INTR_MASK);
        ClearActiveTx();
        progress = true;
      }
    }
    else
    {
      uart_hal_disable_intr_mask(&uart_hal_, UART_TX_INTR_MASK);
      progress = true;
    }
  }

  if (progress && !tx_active_valid_)
  {
    (void)StartNextFifoTx(in_isr, submit);
  }
  return 0U;
}

uint32_t IRAM_ATTR ESP32UART::ServiceFifoIrqSource(bool in_isr) noexcept
{
  const uint32_t status = uart_hal_get_intsts_mask(&uart_hal_);
  uint32_t service_events = 0U;

  if ((status & UART_RX_INTR_MASK) != 0U)
  {
    if ((status & UART_INTR_RXFIFO_OVF) != 0U)
    {
      uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_OVF);
    }

    uart_hal_clr_intsts_mask(&uart_hal_, UART_RX_DATA_INTR_MASK);
    service_events |= FIFO_EVENT_RX_DRAIN;
  }

  if ((status & UART_TX_INTR_MASK) != 0U)
  {
    uart_hal_clr_intsts_mask(&uart_hal_, UART_TX_INTR_MASK);
    service_events |= FIFO_EVENT_IRQ;
  }

  (void)in_isr;
  return service_events;
}

bool IRAM_ATTR ESP32UART::StartNextFifoTx(bool in_isr, FifoSubmitContext* submit)
{
  ASSERT(!tx_active_valid_);

  WriteInfoBlock info{};
  if (write_port_->queue_info_->Peek(info) != ErrorCode::OK)
  {
    return false;
  }

  REQUIRE_FROM_CALLBACK(write_port_->queue_data_ != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(info.data.size_ <= write_port_->queue_data_->Size(), in_isr);
  const bool synchronous_submission = (submit != nullptr) && !submit->resolved &&
                                      (write_port_->queue_info_->Size() == 1U);

  const ErrorCode pop_result = write_port_->queue_info_->Pop(info);
  REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);

  tx_active_length_ = info.data.size_;
  tx_active_offset_ = 0U;
  tx_active_valid_ = true;

  uart_hal_clr_intsts_mask(&uart_hal_, UART_TX_INTR_MASK);
  uart_hal_ena_intr_mask(&uart_hal_, UART_TX_INTR_MASK);
  FillTxFifo(in_isr);

  if (synchronous_submission)
  {
    submit->result = ErrorCode::OK;
    submit->resolved = true;
  }
  else
  {
    write_port_->Finish(in_isr, ErrorCode::OK, info);
  }
  return true;
}

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
    const ErrorCode pop_result = write_port_->queue_data_->PopWithReader(
        chunk_size,
        [this](const uint8_t* src, size_t size) -> ErrorCode
        {
          uint32_t written = 0U;
          uart_hal_write_txfifo(&uart_hal_, src, static_cast<uint32_t>(size), &written);
          return (written == static_cast<uint32_t>(size)) ? ErrorCode::OK
                                                          : ErrorCode::EMPTY;
        });
    REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);
    tx_active_offset_ += chunk_size;
  }
}

bool IRAM_ATTR ESP32UART::DrainRxFifo(bool in_isr)
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

    const ErrorCode push_result = read_port_->queue_data_->PushWithWriter(
        write_len,
        [this](uint8_t* buffer, size_t chunk_size) -> ErrorCode
        {
          int read_len = static_cast<int>(chunk_size);
          uart_hal_read_rxfifo(&uart_hal_, buffer, &read_len);
          return (read_len == static_cast<int>(chunk_size)) ? ErrorCode::OK
                                                            : ErrorCode::EMPTY;
        });
    if ((push_result == ErrorCode::FULL) || (push_result == ErrorCode::EMPTY))
    {
      break;
    }
    REQUIRE_FROM_CALLBACK(push_result == ErrorCode::OK, in_isr);
    pushed_any = true;
  }

  return pushed_any;
}

}  // namespace LibXR
