#pragma once

#include "socket.hpp"

namespace LibXR
{

/**
 * @class TcpClient
 * @brief TCP 客户端抽象类 / Abstract TCP Client Interface
 *
 * 继承自 Socket，提供连接发起方的行为接口。
 */
class TcpClient : public Socket
{
 public:
  TcpClient(size_t read_buffer_size, size_t write_queue_size, size_t write_buffer_size);

  virtual ~TcpClient() = default;

  void Close() override;

  bool Open(IPAddressRaw remote, uint16_t port) override;

  bool IsOpen() const override;

  bool IsConnected() const override;

  Protocol GetProtocol() const override { return Protocol::TCP; }

    /**
   * @brief 获取对端 IP 地址 / Get remote IP address
   * @return 对端 IP 地址 / Remote IP address
   */
  const IPAddressRaw& GetRemoteIP() const { return remote_ip_; }

  /**
   * @brief 获取对端端口 / Get remote port
   * @return 对端端口 / Remote port
   */
  uint16_t GetRemotePort() const { return remote_port_; }

  IPAddressRaw remote_ip_; ///< 对端 IP 地址 / Remote IP address

  uint16_t remote_port_ = 0; ///< 对端端口 / Remote port

  libxr_tcp_handle handle_; ///< TCP 套接字句柄 / TCP socket handle
};

/**
 * @class TcpServer
 * @brief TCP 服务器抽象类 / Abstract TCP Server Interface
 *
 * 提供监听端口与接受连接的接口。
 */
class TcpServer
{
 public:
  virtual ~TcpServer() = default;

  /**
   * @brief 开始监听端口 / Start listening on a port
   * @param port 本地监听端口 / Port to listen on
   * @return true 表示监听成功 / true if listening succeeded
   */
  virtual bool Listen(uint16_t port) = 0;

  /**
   * @brief 接受连接 / Accept a client connection
   * @return 返回已连接的 TcpClient 对象指针 / Returns pointer to accepted client
   *         如果无连接或失败，返回 nullptr / nullptr on failure or timeout
   */
  virtual TcpClient* Accept() = 0;

  /**
   * @brief 回收连接对象（默认 delete 或放回池） / Release a client object
   * @param client 要释放的 TcpClient 指针 / Client pointer to be released
   */
  virtual void Release(TcpClient* client) = 0;

  libxr_tcp_handle handle_;
};

}  // namespace LibXR
