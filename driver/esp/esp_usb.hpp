#pragma once

#include "driver/usb_serial_jtag_select.h"

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "uart.hpp"

namespace LibXR
{
template <size_t BUFFER_SIZE = 256>
class ESP32VirtualUART : public UART
{
 public:
  ESP32VirtualUART(uint32_t rx_queue_size = 5, uint32_t tx_queue_size = 5,
                   int tx_task_prio = 10, uint32_t tx_stack_depth = 2048,
                   int rx_task_prio = 10, uint32_t rx_stack_depth = 2048)
      : UART(rx_queue_size, BUFFER_SIZE, tx_queue_size, BUFFER_SIZE)
  {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = BUFFER_SIZE,
        .rx_buffer_size = BUFFER_SIZE,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));

    write_port_ = WriteFun;
    read_port_ = ReadFun;

    xTaskCreate(TxTaskWrapper, "esp32_vuart_tx", tx_stack_depth, this, tx_task_prio,
                nullptr);
    xTaskCreate(RxTaskWrapper, "esp32_vuart_rx", rx_stack_depth, this, rx_task_prio,
                nullptr);
  }

  static void TxTaskWrapper(void *arg)
  {
    auto *self = static_cast<ESP32VirtualUART *>(arg);
    self->TxTask(self);
  }

  static void RxTaskWrapper(void *arg)
  {
    auto *self = static_cast<ESP32VirtualUART *>(arg);
    self->RxTask(self);
  }

  void TxTask(ESP32VirtualUART *uart)
  {
    WritePort::WriteInfo info;

    while (true)
    {
      if (uart->write_sem_.Wait() != ErrorCode::OK)
      {
        continue;
      }
      if (uart->write_port_.queue_info_->Pop(info) != ErrorCode::OK)
      {
        continue;
      }

      if (uart->write_port_.queue_data_->PopBatch(uart->tx_buffer_, info.size) !=
          ErrorCode::OK)
      {
        info.op.UpdateStatus(false, ErrorCode::EMPTY);
        continue;
      }

      int sent = usb_serial_jtag_write_bytes(uart->tx_buffer_, info.size, portMAX_DELAY);
      if (sent > 0)
      {
        info.op.UpdateStatus(true, ErrorCode::OK);
        continue;
      }
      else
      {
        info.op.UpdateStatus(false, ErrorCode::FAILED);
        continue;
      }
    }
  }

  void RxTask(ESP32VirtualUART *uart)
  {
    ReadInfoBlock block;

    while (true)
    {
      auto avail = usb_serial_jtag_read_ready();
      int len = 0;
      if (!avail)
      {
        len = usb_serial_jtag_read_bytes(uart->rx_buffer_, 1, portMAX_DELAY);
      }
      else
      {
        len = usb_serial_jtag_read_bytes(uart->rx_buffer_, BUFFER_SIZE, 0);
      }
      if (len > 0)
      {
        LibXR::Mutex::LockGuard guard(uart->read_mutex_);
        uart->read_port_.queue_data_->PushBatch(uart->rx_buffer_, len);
        uart->read_port_.ProcessPendingReads();
      }
    }
  }

  static ErrorCode WriteFun(WritePort &port)
  {
    ESP32VirtualUART *uart = CONTAINER_OF(&port, ESP32VirtualUART, write_port_);
    uart->write_sem_.Post();

    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort &port)
  {
    ESP32VirtualUART *uart = CONTAINER_OF(&port, ESP32VirtualUART, read_port_);
    
    LibXR::Mutex::LockGuard guard(uart->read_mutex_);

    port.ProcessPendingReads();

    return ErrorCode::OK;
  }

  ErrorCode SetConfig(UART::Configuration) override { return ErrorCode::OK; }

 private:
  uint8_t tx_buffer_[BUFFER_SIZE];
  uint8_t rx_buffer_[BUFFER_SIZE];

  LibXR::Semaphore write_sem_;
  LibXR::Mutex read_mutex_;
};

}  // namespace LibXR
