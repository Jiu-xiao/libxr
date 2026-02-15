#include "esp_uart.hpp"

#include <algorithm>

#include "esp_attr.h"
#include "soc/uart_periph.h"

namespace
{
constexpr uint32_t kUartRxIntrMask =
    UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF;
constexpr uint32_t kUartTxIntrMask = UART_INTR_TXFIFO_EMPTY;
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

  const esp_err_t err = esp_intr_alloc(uart_periph_signal[uart_num_].irq,
                                       ESP_INTR_FLAG_IRAM,
                                       UartIsrEntry,
                                       this,
                                       &uart_intr_handle_);
  if (err != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }

  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

void ESP32UART::RemoveUartIsr()
{
  if ((uart_intr_handle_ != nullptr) && uart_isr_installed_)
  {
    esp_intr_free(uart_intr_handle_);
    uart_intr_handle_ = nullptr;
    uart_isr_installed_ = false;
  }
}

void IRAM_ATTR ESP32UART::FillTxFifo(bool in_isr)
{
  if (!tx_active_valid_)
  {
    return;
  }

  while (tx_active_offset_ < tx_double_buffer_.GetActiveLength())
  {
    if (uart_hal_get_txfifo_len(&uart_hal_) == 0)
    {
      break;
    }

    uint32_t write_size = 0;
    const uint8_t* src = tx_double_buffer_.ActiveBuffer() + tx_active_offset_;
    const uint32_t remaining = static_cast<uint32_t>(
        tx_double_buffer_.GetActiveLength() - tx_active_offset_);

    uart_hal_write_txfifo(&uart_hal_, src, remaining, &write_size);
    if (write_size == 0)
    {
      break;
    }

    tx_active_offset_ += write_size;
  }

  if (tx_active_offset_ >= tx_double_buffer_.GetActiveLength())
  {
    if (in_isr)
    {
      uart_hal_disable_intr_mask(&uart_hal_, UART_INTR_TXFIFO_EMPTY);
      OnTxTransferDone(true, ErrorCode::OK);
    }
  }
}

void IRAM_ATTR ESP32UART::HandleRxInterrupt(uint32_t uart_intr_status)
{
  if (uart_intr_status & UART_INTR_RXFIFO_OVF)
  {
    uart_hal_rxfifo_rst(&uart_hal_);
    uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_OVF);
  }

  if (uart_intr_status & (UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT))
  {
    while (uart_hal_get_rxfifo_len(&uart_hal_) > 0)
    {
      int read_len = static_cast<int>(std::min<size_t>(
          uart_hal_get_rxfifo_len(&uart_hal_), rx_isr_buffer_size_));
      if (read_len <= 0)
      {
        break;
      }

      uart_hal_read_rxfifo(&uart_hal_, rx_isr_buffer_, &read_len);
      if (read_len <= 0)
      {
        break;
      }

      PushRxBytes(rx_isr_buffer_, static_cast<size_t>(read_len), true);
    }

    uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT);
  }
}

void IRAM_ATTR ESP32UART::HandleTxInterrupt(uint32_t uart_intr_status)
{
  if (uart_intr_status & UART_INTR_TXFIFO_EMPTY)
  {
    FillTxFifo(true);
    uart_hal_clr_intsts_mask(&uart_hal_, UART_INTR_TXFIFO_EMPTY);
  }
}

void IRAM_ATTR ESP32UART::HandleUartInterrupt()
{
  uint32_t uart_intr_status = uart_hal_get_intsts_mask(&uart_hal_);

  while (uart_intr_status != 0)
  {
    if (uart_intr_status & kUartRxIntrMask)
    {
      HandleRxInterrupt(uart_intr_status);
    }

    if (uart_intr_status & kUartTxIntrMask)
    {
      HandleTxInterrupt(uart_intr_status);
    }

    uart_intr_status = uart_hal_get_intsts_mask(&uart_hal_);
  }
}

}  // namespace LibXR
