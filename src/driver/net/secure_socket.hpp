#pragma once

#include "socket.hpp"

namespace LibXR
{

/**
 * @class SecureSocket
 * @brief TLS 安全套接字封装器 / TLS Secure Socket Wrapper
 *
 * 通过封装一个已连接的底层 Socket 实现 TLS 会话，支持证书配置、握手控制等。
 */
class SecureSocket : public Socket
{
 public:
  virtual ~SecureSocket() = default;

  /**
   * @brief 包装底层 Socket / Wrap an existing connected socket
   * @param underlying 已连接的底层 Socket 指针 / Underlying connected socket
   * @return true 如果包装成功 / true if wrapping succeeded
   */
  virtual bool Wrap(Socket* underlying) = 0;

  /**
   * @brief 执行 TLS 握手过程 / Perform TLS handshake
   * @return true 如果握手成功 / true if handshake succeeded
   */
  virtual bool PerformHandshake() = 0;

  /**
   * @brief 设置 CA 根证书（PEM 格式）/ Set CA root certificate
   * @param caPem PEM 格式的 CA 字符串 / CA certificate in PEM format
   */
  virtual void SetCACert(const char* caPem) = 0;

  /**
   * @brief 设置客户端证书和密钥（PEM 格式）/ Set client certificate and key
   * @param cert 客户端证书 / Client certificate in PEM
   * @param key 客户端私钥 / Client private key in PEM
   */
  virtual void SetClientCert(const char* cert, const char* key) = 0;

  /**
   * @brief 判断当前连接是否为加密连接 / Check if current connection is secure
   * @return true 表示已启用 TLS / true if secured by TLS
   */
  virtual bool IsSecure() const = 0;
};

}  // namespace LibXR
