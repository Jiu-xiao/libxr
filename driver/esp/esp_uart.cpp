#include "esp_uart.hpp"

#include "libxr_rw.hpp"

using namespace LibXR;

size_t ESP32UARTReadPort::EmptySize()
{
  size_t ans = 0;
  uart_get_buffered_data_len(uart_->port_, &ans);
  return uart_->rx_buff_.size_ - ans;
}

size_t ESP32UARTReadPort::Size()
{
  size_t ans = 0;
  uart_get_buffered_data_len(uart_->port_, &ans);
  return ans;
}

void ESP32UARTReadPort::ProcessPendingReads(bool in_isr)
{
  LibXR::Mutex::LockGuard guard(mutex_);
  if (busy_.load(std::memory_order_relaxed) == BusyState::Pending)
  {
    if (Size() >= info_.data.size_)
    {
      int len = uart_read_bytes(uart_->port_, info_.data.addr_, info_.data.size_, 0);
      busy_.store(BusyState::Idle, std::memory_order_relaxed);
      if (len == info_.data.size_)
      {
        Finish(in_isr, ErrorCode::OK, info_, info_.data.size_);
      }
      else
      {
        Finish(in_isr, ErrorCode::EMPTY, info_, len);
      }
    }
  }
}

size_t ESP32UARTWritePort::EmptySize()
{
  size_t ans = 0;
  uart_get_tx_buffer_free_size(uart_->port_, &ans);
  return ans;
}

size_t ESP32UARTWritePort::Size()
{
  size_t ans = 0;
  uart_get_tx_buffer_free_size(uart_->port_, &ans);
  return uart_->tx_buff_.size_ - ans;
}

ESP32UART::ESP32UART(uart_port_t port, int tx_pin, int rx_pin, uint32_t buffer_size,
                     uint32_t rx_thread_stack_depth, uint32_t rx_thread_priority)
    : UART(&_read_port, &_write_port),
      port_(port),
      rx_buff_(new uint8_t[buffer_size], buffer_size),
      tx_buff_(new uint8_t[buffer_size], buffer_size),
      _read_port(0, this),
      _write_port(1, 0, this)
{
  uart_config_t config = {};
  config.baud_rate = 115200;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_APB;

  ESP_ERROR_CHECK(uart_param_config(port_, &config));
  ESP_ERROR_CHECK(
      uart_set_pin(port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(
      uart_driver_install(port_, rx_buff_.size_, tx_buff_.size_, 20, &event_queue_, 0));

  _read_port = ReadFun;
  _write_port = WriteFun;

  xTaskCreate(RxTask, "uart_rx_task", rx_thread_stack_depth, this, rx_thread_priority,
              nullptr);
}

void ESP32UART::RxTask(void *param)
{
  auto *self = static_cast<ESP32UART *>(param);
  uart_event_t event;
  while (true)
  {
    if (xQueueReceive(self->event_queue_, &event, portMAX_DELAY))
    {
      self->HandleEvent(event);
    }
  }
}

void ESP32UART::HandleEvent(const uart_event_t &event)
{
  switch (event.type)
  {
    case UART_DATA:
    {
      read_port_->ProcessPendingReads(false);
      break;
    }
    case UART_BUFFER_FULL:
    case UART_FIFO_OVF:
    case UART_FRAME_ERR:
    case UART_PARITY_ERR:
    {
      LibXR::Mutex::LockGuard guard(read_port_->mutex_);

      if (read_port_->busy_.load(std::memory_order_relaxed) ==
          LibXR::ReadPort::BusyState::Pending)
      {
        read_port_->info_.op.UpdateStatus(false, ErrorCode::FAILED);
      }
      uart_flush_input(port_);
      read_port_->Reset();
      write_port_->Reset();
      break;
    }
    default:
      break;
  }
}

ErrorCode ESP32UART::WriteFun(WritePort &port)
{
  ESP32UART *self = CONTAINER_OF(&port, ESP32UART, _write_port);
  WriteInfoBlock info;
  if (port.queue_info_->Pop(info) != ErrorCode::OK)
  {
    return ErrorCode::EMPTY;
  }

  size_t space = 0;

  uart_get_tx_buffer_free_size(self->port_, &space);
  if (space < info.data.size_)
  {
    return ErrorCode::FULL;
  }

  uart_write_bytes(self->port_, static_cast<const char *>(info.data.addr_),
                   info.data.size_);

  return ErrorCode::OK;
}

ErrorCode ESP32UART::ReadFun(ReadPort &port)
{
  ESP32UART *self = CONTAINER_OF(&port, ESP32UART, _read_port);
  UNUSED(self);

  if (port.Size() >= port.info_.data.size_)
  {
    int len =
        uart_read_bytes(self->port_, port.info_.data.addr_, port.info_.data.size_, 0);

    port.read_size_ = len;
    if (len == port.info_.data.size_)
    {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::EMPTY;
}

ErrorCode ESP32UART::SetConfig(UART::Configuration config)
{
  uart_config_t uart_cfg = {};

  uart_cfg.baud_rate = static_cast<int>(config.baudrate);
  uart_cfg.data_bits = UART_DATA_8_BITS;
  uart_cfg.parity = UART_PARITY_DISABLE;
  uart_cfg.stop_bits = UART_STOP_BITS_1;
  uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_cfg.source_clk = UART_SCLK_APB;

  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      uart_cfg.parity = UART_PARITY_DISABLE;
      uart_cfg.data_bits = UART_DATA_8_BITS;
      break;
    case UART::Parity::EVEN:
      uart_cfg.parity = UART_PARITY_EVEN;
      uart_cfg.data_bits = UART_DATA_8_BITS;
      break;
    case UART::Parity::ODD:
      uart_cfg.parity = UART_PARITY_ODD;
      uart_cfg.data_bits = UART_DATA_8_BITS;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  switch (config.stop_bits)
  {
    case 1:
      uart_cfg.stop_bits = UART_STOP_BITS_1;
      break;
    case 2:
      uart_cfg.stop_bits = UART_STOP_BITS_2;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  if (uart_param_config(port_, &uart_cfg) != ESP_OK)
  {
    return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}
