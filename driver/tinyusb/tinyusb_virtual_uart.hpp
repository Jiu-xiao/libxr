#pragma once

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "tusb.h"
#include "uart.hpp"

namespace LibXR
{

class TinyUSBVirtualUART;

// Read端口
class TinyUSBUARTReadPort : public ReadPort
{
 public:
  TinyUSBUARTReadPort(TinyUSBVirtualUART *uart) : ReadPort(0), uart_(uart) {}

  size_t EmptySize();
  size_t Size();
  void ProcessPendingReads(bool in_isr = true);
  void Reset() { read_size_ = 0; }

  using ReadPort::operator=;

 private:
  TinyUSBVirtualUART *uart_;
};

// Write端口
class TinyUSBUARTWritePort : public WritePort
{
 public:
  TinyUSBUARTWritePort(TinyUSBVirtualUART *uart) : WritePort(1, 0), uart_(uart) {}

  size_t EmptySize();
  size_t Size();
  void Reset() { write_size_ = 0; }

  using WritePort::operator=;

 private:
  TinyUSBVirtualUART *uart_;
};

// 主类
class TinyUSBVirtualUART : public UART
{
 public:
  TinyUSBVirtualUART(size_t packet_size = 64);

  static ErrorCode WriteFun(WritePort &port);
  static ErrorCode ReadFun(ReadPort &port);
  ErrorCode SetConfig(UART::Configuration config) override;

  void Poll() { tud_task(); }
  size_t Available() const { return tud_cdc_available(); }
  bool Connected() const { return tud_cdc_connected(); }
  size_t MaxPacketSize() const { return packet_size_; }

  TinyUSBUARTReadPort _read_port;
  TinyUSBUARTWritePort _write_port;

  friend TinyUSBUARTWritePort;
  friend TinyUSBUARTReadPort;
  friend void tud_cdc_rx_cb(uint8_t itf);

  static inline TinyUSBVirtualUART *self;

 private:
  size_t packet_size_;
  bool reading_;
};

}  // namespace LibXR
