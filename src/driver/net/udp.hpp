#pragma once

#include "socket.hpp"

namespace LibXR
{

/**
 * @class UdpSocket
 * @brief UDP 套接字抽象类 / Abstract UDP Socket Interface
 *
 * 提供无连接的数据报通信接口，继承自 Socket。
 * 支持绑定、发送到指定目标、接收来源地址。
 */
class UdpSocket : public Socket
{
 public:
  virtual ~UdpSocket() = default;

  /**
   * @brief 绑定本地端口 / Bind to a local port
   * @param local_port 要绑定的端口号 / Port number to bind
   * @return true 表示绑定成功 / true if successfully bound
   */
  virtual bool Bind(uint16_t local_port) = 0;

  /**
   * @brief 向目标地址发送数据 / Send data to a remote IP/port
   * @param dst 目标 IP 地址 / Destination IP address
   * @param port 目标端口 / Destination port
   * @param data 数据指针 / Pointer to data to send
   * @param len 数据长度 / Length of data in bytes
   * @return true 表示发送成功 / true if sent successfully
   */
  virtual bool SendTo(IPAddressRaw dst, uint16_t port, const void* data, size_t len) = 0;

  /**
   * @brief 从任意来源接收数据 / Receive data from any source
   * @param src 接收到的数据源地址 / Source IP address
   * @param port 接收到的数据源端口 / Source port
   * @param buffer 用于接收的缓冲区 / Buffer for received data
   * @param maxlen 缓冲区最大长度 / Max buffer size in bytes
   * @return 实际接收到的字节数（负数表示失败）/ Bytes received, or negative on error
   */
  virtual int ReceiveFrom(IPAddressRaw& src, uint16_t& port, void* buffer,
                          size_t maxlen) = 0;
};

}  // namespace LibXR
