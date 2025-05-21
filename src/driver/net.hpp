#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>

namespace LibXR
{

constexpr size_t IPADDR_STRLEN = 16;   // "255.255.255.255" + '\0'
constexpr size_t MACADDR_STRLEN = 18;  // "FF:FF:FF:FF:FF:FF" + '\0'

/**
 * @brief 原始 IPv4 地址 / Raw IPv4 address
 */
struct IPAddressRaw
{
  uint8_t bytes[4]{};

  static IPAddressRaw FromString(const char* str)
  {
    IPAddressRaw ip{};
    std::sscanf(str, "%hhu.%hhu.%hhu.%hhu",
                &ip.bytes[0], &ip.bytes[1], &ip.bytes[2], &ip.bytes[3]);
    return ip;
  }

  void ToString(char out[IPADDR_STRLEN]) const
  {
    std::snprintf(out, IPADDR_STRLEN, "%u.%u.%u.%u",
                  bytes[0], bytes[1], bytes[2], bytes[3]);
  }

  bool operator==(const IPAddressRaw& other) const
  {
    return std::memcmp(bytes, other.bytes, 4) == 0;
  }

  bool operator!=(const IPAddressRaw& other) const
  {
    return !(*this == other);
  }
};

/**
 * @brief 字符串形式 IPv4 地址 / IPv4 address as string
 */
struct IPAddressStr
{
  char str[IPADDR_STRLEN]{};

  static IPAddressStr FromRaw(const IPAddressRaw& raw)
  {
    IPAddressStr s{};
    raw.ToString(s.str);
    return s;
  }

  IPAddressRaw ToRaw() const
  {
    return IPAddressRaw::FromString(str);
  }
};

/**
 * @brief 原始 MAC 地址 / Raw MAC address
 */
struct MACAddressRaw
{
  uint8_t bytes[6]{};

  static MACAddressRaw FromString(const char* str)
  {
    MACAddressRaw mac{};
    std::sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &mac.bytes[0], &mac.bytes[1], &mac.bytes[2],
                &mac.bytes[3], &mac.bytes[4], &mac.bytes[5]);
    return mac;
  }

  void ToString(char out[MACADDR_STRLEN]) const
  {
    std::snprintf(out, MACADDR_STRLEN, "%02X:%02X:%02X:%02X:%02X:%02X",
                  bytes[0], bytes[1], bytes[2],
                  bytes[3], bytes[4], bytes[5]);
  }

  bool operator==(const MACAddressRaw& other) const
  {
    return std::memcmp(bytes, other.bytes, 6) == 0;
  }

  bool operator!=(const MACAddressRaw& other) const
  {
    return !(*this == other);
  }
};

/**
 * @brief 字符串形式 MAC 地址 / MAC address as string
 */
struct MACAddressStr
{
  char str[MACADDR_STRLEN]{};

  static MACAddressStr FromRaw(const MACAddressRaw& raw)
  {
    MACAddressStr s{};
    raw.ToString(s.str);
    return s;
  }

  MACAddressRaw ToRaw() const
  {
    return MACAddressRaw::FromString(str);
  }
};

/**
 * @brief 抽象网络接口类 / Abstract base for network interfaces
 */
class NetworkInterface
{
 public:
  virtual ~NetworkInterface() = default;

  virtual bool Enable() = 0;
  virtual void Disable() = 0;
  virtual bool IsConnected() const = 0;

  virtual IPAddressRaw GetIPAddress() const = 0;
  virtual MACAddressRaw GetMACAddress() const = 0;
};

}  // namespace LibXR
