#pragma once

#include "libxr_rw.hpp"
#include "net.hpp"

namespace LibXR
{

/**
 * @class Socket
 * @brief 通用 Socket 抽象类（TCP / UDP）/ Abstract base for Socket (TCP/UDP)
 *
 * 所有 Socket 类型（包括 TcpClient, UdpSocket, SecureSocket）应继承自本类，
 * 并实现 Open / Close / IsOpen 接口。同时继承 ReadPort / WritePort，
 * 可与 I/O 框架无缝集成。
 */
class Socket : public ReadPort, public WritePort
{
 public:
  enum class Protocol
  {
    TCP,
    UDP
  };

  Socket(size_t read_buffer_size = 512, size_t write_queue_size = 4,
         size_t write_buffer_size = 512)
      : ReadPort(read_buffer_size), WritePort(write_queue_size, write_buffer_size)
  {
  }

  virtual ~Socket() = default;

  /**
   * @brief 打开连接 / Open connection
   * @param remote 对端地址 / Remote IP address
   * @param port   对端端口 / Remote port
   * @return true 表示成功打开 / true if successfully opened
   */
  virtual bool Open(IPAddressRaw remote, uint16_t port) = 0;

  /**
   * @brief 关闭连接 / Close the socket
   */
  virtual void Close() = 0;

  /**
   * @brief 检查连接是否打开 / Check if socket is open
   * @return true if open
   */
  virtual bool IsOpen() const = 0;

  /**
   * @brief 检查是否已连接 / Check if connected
   *
   * @return true
   * @return false
   */
  virtual bool IsConnected() const = 0;

  /**
   * @brief 获取 Socket 协议类型 / Get socket protocol type
   */
  virtual Protocol GetProtocol() const = 0;

  template <typename OperationType, typename = std::enable_if_t<std::is_base_of_v<
                                        WriteOperation, std::decay_t<OperationType>>>>
  ErrorCode Write(ConstRawData data, OperationType&& op)
  {
    return WritePort::operator()(data, std::forward<OperationType>(op));
  }

  template <typename OperationType, typename = std::enable_if_t<std::is_base_of_v<
                                        ReadOperation, std::decay_t<OperationType>>>>
  ErrorCode Read(RawData data, OperationType&& op)
  {
    return ReadPort::operator()(data, std::forward<OperationType>(op));
  }
};

}  // namespace LibXR
