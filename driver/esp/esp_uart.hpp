#pragma once

#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

namespace LibXR
{
class ESP32UART;
class ESP32UARTReadPort : public ReadPort
{
 public:
  ESP32UARTReadPort(size_t buffer_size, ESP32UART *uart)
      : ReadPort(buffer_size), uart_(uart)
  {
  }

  size_t EmptySize();

  size_t Size();

  void ProcessPendingReads(bool in_isr = true);

  void Reset() { read_size_ = 0; }

  using ReadPort::operator=;

 private:
  ESP32UART *uart_;
};

class ESP32UARTWritePort : public WritePort
{
 public:
  ESP32UARTWritePort(size_t queue_size, size_t buffer_size,
                     ESP32UART *uart)
      : WritePort(queue_size, buffer_size), uart_(uart)
  {
  }

  size_t EmptySize();

  size_t Size();

  void Reset() { write_size_ = 0; }

  using WritePort::operator=;

 private:
  ESP32UART *uart_;
};

class ESP32UART : public UART
{
 public:
  ESP32UART(uart_port_t port, int tx_pin, int rx_pin, uint32_t buffer_size = 256,
            uint32_t rx_thread_stack_depth = 1024, uint32_t rx_thread_priority = 10);

  static ErrorCode WriteFun(WritePort &port);
  static ErrorCode ReadFun(ReadPort &port);

  ErrorCode SetConfig(UART::Configuration config) override;

  friend class ESP32UARTReadPort;
  friend class ESP32UARTWritePort;

 private:
  static void RxTask(void *param);
  static void TxTask(void *param);
  void HandleEvent(const uart_event_t &event);

  uart_port_t port_;
  QueueHandle_t event_queue_;
  RawData rx_buff_;
  RawData tx_buff_;

  ESP32UARTReadPort _read_port;
  ESP32UARTWritePort _write_port;
};

}  // namespace LibXR
