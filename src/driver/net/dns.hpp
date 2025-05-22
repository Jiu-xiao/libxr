#pragma once

#include "net.hpp"

namespace LibXR
{

/**
 * @class DnsResolver
 * @brief 域名解析器抽象接口 / Abstract DNS Resolver Interface
 *
 * 提供基础的主机名到 IP 地址的解析功能，适配不同平台（如 ESP-IDF、LwIP、POSIX）。
 */
class DnsResolver
{
 public:
  virtual ~DnsResolver() = default;

  /**
   * @brief 解析域名 / Resolve a hostname to an IP address
   * @param hostname 要解析的主机名 / Hostname to resolve
   * @param outIp 解析结果 IP 地址输出 / Output: resolved IP address
   * @return true 表示解析成功 / true if successfully resolved
   */
  virtual bool Resolve(const char* hostname, IPAddressRaw& outIp) = 0;
};

}  // namespace LibXR
