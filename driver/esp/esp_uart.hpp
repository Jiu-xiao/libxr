#pragma once

#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

namespace LibXR
{

class ESP32UART : public UART
{
 public:
  ESP32UART(uart_port_t port, int tx_pin, int rx_pin, uint32_t buffer_size = 256,
            uint32_t rx_queue_size = 5, uint32_t tx_queue_size = 15,
            uint32_t tx_thread_stack_depth = 1024, uint32_t tx_thread_priority = 10,
            uint32_t rx_thread_stack_depth = 1024, uint32_t rx_thread_priority = 10);

  static ErrorCode WriteFun(WritePort &port);
  static ErrorCode ReadFun(ReadPort &port);

  ErrorCode SetConfig(UART::Configuration config) override;

 private:
  static void RxTask(void *param);
  static void TxTask(void *param);
  void HandleEvent(const uart_event_t &event);

  uart_port_t port_;
  QueueHandle_t event_queue_;
  SemaphoreHandle_t write_sem_;
  RawData rx_buff_;
  RawData tx_buff_;
};

}  // namespace LibXR
