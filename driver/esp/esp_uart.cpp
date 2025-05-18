#include "esp_uart.hpp"

using namespace LibXR;

ESP32UART::ESP32UART(uart_port_t port, int tx_pin, int rx_pin, uint32_t buffer_size,
                     uint32_t rx_queue_size, uint32_t tx_queue_size,
                     uint32_t tx_thread_stack_depth, uint32_t tx_thread_priority,
                     uint32_t rx_thread_stack_depth, uint32_t rx_thread_priority)
    : UART(rx_queue_size, buffer_size, tx_queue_size, buffer_size),
      port_(port),
      rx_buff_(new uint8_t[buffer_size], buffer_size),
      tx_buff_(new uint8_t[buffer_size], buffer_size)
{
  uart_config_t config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
  };
  ESP_ERROR_CHECK(uart_param_config(port_, &config));
  ESP_ERROR_CHECK(
      uart_set_pin(port_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(
      uart_driver_install(port_, rx_buff_.size_, tx_buff_.size_, 20, &event_queue_, 0));

  write_sem_ = xSemaphoreCreateBinary();

  read_port_ = ReadFun;
  write_port_ = WriteFun;

  xTaskCreate(RxTask, "uart_rx_task", rx_thread_stack_depth, this, rx_thread_priority,
              nullptr);
  xTaskCreate(TxTask, "uart_tx_task", tx_thread_stack_depth, this, tx_thread_priority,
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

void ESP32UART::TxTask(void *param)
{
  auto *self = static_cast<ESP32UART *>(param);
  WritePort::WriteInfo info;
  while (true)
  {
    if (xSemaphoreTake(self->write_sem_, portMAX_DELAY) != pdTRUE)
    {
      continue;
    }

    if (self->write_port_.queue_info_->Pop(info) != ErrorCode::OK)
    {
      continue;
    }

    if (self->write_port_.queue_data_->PopBatch(
            reinterpret_cast<uint8_t *>(self->tx_buff_.addr_), info.size) !=
        ErrorCode::OK)
    {
      info.op.UpdateStatus(false, ErrorCode::EMPTY);
      continue;
    }

    int written = uart_write_bytes(
        self->port_, static_cast<const char *>(self->tx_buff_.addr_), info.size);

    if (written <= 0)
    {
      info.op.UpdateStatus(false, ErrorCode::FAILED);
    }
    else
    {
      self->write_port_.write_size_ = info.size;
      info.op.UpdateStatus(true, ErrorCode::OK);
    }
  }
}

void ESP32UART::HandleEvent(const uart_event_t &event)
{
  switch (event.type)
  {
    case UART_DATA:
    {
      uint8_t *buf = static_cast<uint8_t *>(rx_buff_.addr_);
      int len = uart_read_bytes(port_, buf, rx_buff_.size_, 0);
      if (len > 0)
      {
        read_port_.queue_data_->PushBatch(buf, len);
        read_port_.ProcessPendingReads();
      }
      break;
    }
    case UART_BUFFER_FULL:
    case UART_FIFO_OVF:
    case UART_FRAME_ERR:
    case UART_PARITY_ERR:
    {
      uart_flush_input(port_);
      read_port_.Reset();
      write_port_.Reset();
      break;
    }
    default:
      break;
  }
}

ErrorCode ESP32UART::WriteFun(WritePort &port)
{
  ESP32UART *self = CONTAINER_OF(&port, ESP32UART, write_port_);
  xSemaphoreGive(self->write_sem_);
  return ErrorCode::OK;
}

ErrorCode ESP32UART::ReadFun(ReadPort &port)
{
  ESP32UART *self = CONTAINER_OF(&port, ESP32UART, read_port_);
  port.ProcessPendingReads();
  return ErrorCode::OK;
}

ErrorCode ESP32UART::SetConfig(UART::Configuration config)
{
  uart_config_t uart_cfg = {
      .baud_rate = static_cast<int>(config.baudrate),
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
  };

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
