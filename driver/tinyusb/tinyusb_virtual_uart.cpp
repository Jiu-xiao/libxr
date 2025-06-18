#include "tinyusb_virtual_uart.hpp"

using namespace LibXR;

/**
 * @brief CDC 接收回调（由TinyUSB调用）
 * @param itf 接口号
 */
extern "C" void tud_cdc_rx_cb(uint8_t itf)
{
  UNUSED(itf);
  if (TinyUSBVirtualUART::self)
  {
    auto &port = TinyUSBVirtualUART::self->_read_port;
    size_t avail = tud_cdc_available();
    if (avail > 0)
    {
      port.ProcessPendingReads(true);
    }
  }
}

// ==================== TinyUSBUARTReadPort ====================

/**
 * @brief 返回FIFO可用空间（空余字节数）
 * @return 可用空间字节数
 */
size_t TinyUSBUARTReadPort::EmptySize()
{
  // TinyUSB FIFO最大容量 - 已有数据
  return CFG_TUD_CDC_RX_BUFSIZE - tud_cdc_available();
}

/**
 * @brief 获取当前已用空间（接收数据量）
 * @return 已接收字节数
 */
size_t TinyUSBUARTReadPort::Size() { return tud_cdc_available(); }

/**
 * @brief 处理等待中的读操作
 * @param in_isr 是否在中断中调用
 */
void TinyUSBUARTReadPort::ProcessPendingReads(bool in_isr)
{
  BusyState curr = busy_.load(std::memory_order_relaxed);

  if (curr == BusyState::Pending)
  {
    if (Size() >= info_.data.size_)
    {
      int len = tud_cdc_read(static_cast<uint8_t *>(info_.data.addr_), info_.data.size_);
      busy_.store(BusyState::Idle, std::memory_order_release);

      if (len == static_cast<int>(info_.data.size_))
      {
        Finish(in_isr, ErrorCode::OK, info_, info_.data.size_);
      }
      else
      {
        Finish(in_isr, ErrorCode::EMPTY, info_, len);
      }
    }
  }
  else if (curr == BusyState::Idle)
  {
    busy_.store(BusyState::Event, std::memory_order_release);
  }
}

// ==================== TinyUSBUARTWritePort ====================

/**
 * @brief 返回FIFO可写空间（空余字节数）
 * @return 可用写入空间字节数
 */
size_t TinyUSBUARTWritePort::EmptySize() { return tud_cdc_write_available(); }

/**
 * @brief 获取已写入空间（写入但未发送的字节数）
 * @return 已写入字节数
 */
size_t TinyUSBUARTWritePort::Size()
{
  return CFG_TUD_CDC_TX_BUFSIZE - tud_cdc_write_available();
}

// ==================== TinyUSBVirtualUART ====================

/**
 * @brief TinyUSB虚拟串口构造
 * @param packet_size USB包大小
 */
TinyUSBVirtualUART::TinyUSBVirtualUART()
    : UART(&_read_port, &_write_port), _read_port(this), _write_port(this)
{
  self = this;
  _read_port = ReadFun;
  _write_port = WriteFun;
  tusb_init();
}

/**
 * @brief 写操作实现
 * @param port 写端口引用
 * @return 错误码
 */
ErrorCode TinyUSBVirtualUART::WriteFun(WritePort &port)
{
  WriteInfoBlock info;
  if (port.queue_info_->Pop(info) != ErrorCode::OK) return ErrorCode::EMPTY;

  size_t space = tud_cdc_write_available();
  if (space < info.data.size_) return ErrorCode::FULL;

  size_t written =
      tud_cdc_write(static_cast<const uint8_t *>(info.data.addr_), info.data.size_);
  tud_cdc_write_flush();

  if (written == info.data.size_)
  {
    port.Finish(false, ErrorCode::OK, info, written);
    return ErrorCode::OK;
  }
  else
  {
    port.Finish(false, ErrorCode::FAILED, info, written);
    return ErrorCode::FAILED;
  }
}

/**
 * @brief 读操作实现
 * @param port 读端口引用
 * @return 错误码
 */
ErrorCode TinyUSBVirtualUART::ReadFun(ReadPort &port)
{
  if (tud_cdc_available() >= port.info_.data.size_)
  {
    int len = tud_cdc_read(static_cast<uint8_t *>(port.info_.data.addr_),
                           port.info_.data.size_);
    port.read_size_ = len;
    return ErrorCode::OK;
  }
  return ErrorCode::EMPTY;
}

/**
 * @brief 配置CDC，无实际动作
 * @param 配置参数
 * @return 错误码
 */
ErrorCode TinyUSBVirtualUART::SetConfig(UART::Configuration)
{
  // CDC无须配置
  return ErrorCode::OK;
}
