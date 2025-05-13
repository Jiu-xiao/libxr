#pragma once

#include <string>
#include <vector>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief Wifi 客户端接口类 / Interface class for WiFi client management
 */
class WifiClient
{
 public:
  /**
   * @brief WiFi 错误码枚举 / Enumeration of WiFi error codes
   */
  enum class WifiError
  {
    NONE,                   ///< 无错误 / No error
    ALREADY_ENABLED,        ///< 已启用 / Already enabled
    NOT_ENABLED,            ///< 未启用 / Not enabled
    CONNECTION_TIMEOUT,     ///< 连接超时 / Connection timeout
    AUTHENTICATION_FAILED,  ///< 身份验证失败 / Authentication failed
    DHCP_FAILED,            ///< DHCP 获取失败 / DHCP acquisition failed
    SSID_NOT_FOUND,         ///< 找不到 SSID / SSID not found
    INVALID_CONFIG,         ///< 配置无效 / Invalid configuration
    HARDWARE_FAILURE,       ///< 硬件故障 / Hardware failure
    SCAN_FAILED,            ///< 扫描失败 / Scan failed
    UNKNOWN,                ///< 未知错误 / Unknown error
  };

  /**
   * @brief WiFi 安全类型 / WiFi security types
   */
  enum class Security
  {
    OPEN,             ///< 开放网络 / Open network
    WPA2_PSK,         ///< WPA2-PSK / WPA2-PSK
    WPA2_ENTERPRISE,  ///< WPA2 企业认证 / WPA2 Enterprise
    UNKNOWN,          ///< 未知类型 / Unknown type
  };

  /**
   * @brief 企业 WiFi 配置 / Enterprise WiFi configuration
   */
  struct EnterpriseConfig
  {
    std::string identity;     ///< EAP 身份标识 / EAP identity
    std::string username;     ///< 用户名 / Username
    std::string password;     ///< 密码 / Password
    std::string ca_cert;      ///< CA 证书路径 / CA certificate path
    std::string client_cert;  ///< 客户端证书路径 / Client certificate path
    std::string client_key;   ///< 客户端密钥路径 / Client key path
  };

  /**
   * @brief 静态 IP 配置 / Static IP configuration
   */
  struct StaticIPConfig
  {
    std::string ip;       ///< IP 地址 / IP address
    std::string gateway;  ///< 网关地址 / Gateway address
    std::string netmask;  ///< 子网掩码 / Netmask
    std::string dns;      ///< DNS 服务器 / DNS server
  };

  /**
   * @brief WiFi 连接配置 / WiFi connection configuration
   */
  struct Config
  {
    std::string ssid;                        ///< SSID 名称 / SSID name
    std::string password;                    ///< 密码 / Password
    Security security = Security::WPA2_PSK;  ///< 安全类型 / Security type

    EnterpriseConfig* enterprise_config =
        nullptr;  ///< 企业认证配置（可选） / Enterprise authentication config (optional)

    StaticIPConfig* static_ip_config =
        nullptr;  ///< 静态 IP 配置（可选） / Static IP config (optional)

    bool use_dhcp = true;  ///< 是否使用 DHCP / Whether to use DHCP
  };

  /**
   * @brief WiFi 扫描结果 / WiFi scan result
   */
  struct ScanResult
  {
    std::string ssid;   ///< 发现的 SSID / Detected SSID
    int rssi;           ///< 信号强度 / Signal strength (RSSI)
    Security security;  ///< 安全类型 / Security type
  };

  /**
   * @brief WiFi 状态回调 / Callback type for WiFi status
   */
  using WifiCallback = LibXR::Callback<WifiError>;

  virtual ~WifiClient() = default;

  /**
   * @brief 启用 WiFi 模块 / Enable the WiFi module
   */
  virtual WifiError Enable() = 0;

  /**
   * @brief 禁用 WiFi 模块 / Disable the WiFi module
   */
  virtual WifiError Disable() = 0;

  /**
   * @brief 连接到 WiFi 网络 / Connect to a WiFi network
   * @param config 配置参数 / Configuration parameters
   */
  virtual WifiError Connect(const Config& config) = 0;

  /**
   * @brief 断开当前 WiFi 连接 / Disconnect from the current WiFi connection
   */
  virtual WifiError Disconnect() = 0;

  /**
   * @brief 检查是否已连接 / Check if currently connected
   * @return true 表示已连接 / true if connected
   */
  virtual bool IsConnected() const = 0;

  /**
   * @brief 获取当前 IP 地址 / Get the current IP address
   * @return IP 字符串 / IP address string
   */
  virtual const char* GetIPAddress() const = 0;

  /**
   * @brief 扫描可用 WiFi 网络 / Scan for available WiFi networks
   * @param results 扫描结果列表 / Output list of scan results
   */
  virtual WifiError Scan(std::vector<ScanResult>& results) = 0;

  /**
   * @brief 获取当前 WiFi 信号强度 / Get the current WiFi signal strength
   * 
   * @return int 
   */
  virtual int GetRSSI() const = 0;
};

}  // namespace LibXR
