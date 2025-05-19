#pragma once

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
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
      : UART(&_read_port, &_write_port),
        _read_port(rx_queue_size),
        _write_port(BUFFER_SIZE, tx_queue_size)
  {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = BUFFER_SIZE,
        .rx_buffer_size = BUFFER_SIZE,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));

    _write_port = WriteFun;
    _read_port = ReadFun;

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
    WriteInfoBlock info;

    while (true)
    {
      if (uart->write_sem_.Wait() != ErrorCode::OK)
      {
        continue;
      }
      if (uart->write_port_->queue_info_->Pop(info) != ErrorCode::OK)
      {
        continue;
      }

      if (uart->write_port_->queue_data_->PopBatch(uart->tx_buffer_, info.data.size_) !=
          ErrorCode::OK)
      {
        uart->write_port_->Finish(false, ErrorCode::FAILED, info, info.data.size_);
        continue;
      }

      int sent =
          usb_serial_jtag_write_bytes(uart->tx_buffer_, info.data.size_, portMAX_DELAY);
      if (sent == info.data.size_)
      {
        uart->write_port_->Finish(false, ErrorCode::OK, info, sent);
        continue;
      }
      else
      {
        uart->write_port_->Finish(false, ErrorCode::FAILED, info, sent);
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
        LibXR::Mutex::LockGuard guard(uart->read_port_->mutex_);
        uart->read_port_->queue_data_->PushBatch(uart->rx_buffer_, len);
        uart->read_port_->ProcessPendingReads(false);
      }
    }
  }

  static ErrorCode WriteFun(WritePort &port)
  {
    ESP32VirtualUART *uart = CONTAINER_OF(&port, ESP32VirtualUART, _write_port);
    uart->write_sem_.Post();

    return ErrorCode::FAILED;
  }

  static ErrorCode ReadFun(ReadPort &port)
  {
    ReadInfoBlock &block = port.info_;

    block.op.MarkAsRunning();

    if (port.queue_data_->Size() >= block.data.size_)
    {
      port.queue_data_->PopBatch(block.data.addr_, block.data.size_);
      port.read_size_ = block.data.size_;
      block.op.UpdateStatus(false, ErrorCode::OK);
      return ErrorCode::OK;
    }

    return ErrorCode::EMPTY;
  }

  ErrorCode SetConfig(UART::Configuration) override { return ErrorCode::OK; }

 private:
  uint8_t tx_buffer_[BUFFER_SIZE];
  uint8_t rx_buffer_[BUFFER_SIZE];

  LibXR::Semaphore write_sem_;

  ReadPort _read_port;
  WritePort _write_port;
};

}  // namespace LibXR
