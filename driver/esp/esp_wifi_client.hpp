#pragma once

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "net/wifi_client.hpp"

namespace LibXR
{

/**
 * @class ESP32WifiClient
 * @brief ESP32 平台的 WiFi 客户端实现 / WiFi client implementation for ESP32
 *
 * 提供基于 ESP-IDF 的 WiFi 接口实现，包含连接、断开、扫描、RSSI 查询等功能，
 * 继承自抽象类 WifiClient 并实现其所有接口。
 */
class ESP32WifiClient : public WifiClient
{
 public:
  ESP32WifiClient();

  ~ESP32WifiClient() override;

  bool Enable() override;

  void Disable() override;

  WifiError Connect(const Config& config) override;

  WifiError Disconnect() override;

  bool IsConnected() const override;

  IPAddressRaw GetIPAddress() const override;

  MACAddressRaw GetMACAddress() const override;

  WifiError Scan(ScanResult* out_list, size_t max_count, size_t& out_found) override;

  int GetRSSI() const override;

 private:
  /**
   * @brief 事件处理回调 / Event handler callback
   * @param arg 用户数据 / User pointer
   * @param event_base 事件域 / Event base type
   * @param event_id 事件编号 / Event ID
   * @param event_data 附加数据 / Event payload
   */
  static void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                           void* event_data);

  static inline bool is_initialized_ =
      false;  ///< ESP 网络是否已初始化 / Netif initialized
  static inline esp_netif_t* netif_ = nullptr;  ///< ESP 默认 netif 对象 / Default netif

  bool enabled_ = false;    ///< 是否启用 / Whether WiFi is enabled
  bool connected_ = false;  ///< 是否连接 / Whether WiFi is connected
  bool got_ip_ = false;     ///< 是否获取 IP / Whether IP is acquired
  char ip_str_[16] = {};    ///< 当前 IP 字符串 / Current IP string
  uint8_t mac_[6] = {};     ///< 当前 MAC 缓存 / Cached MAC address

  LibXR::Semaphore semaphore_;  ///< 状态同步信号量 / Event wait semaphore
};

}  // namespace LibXR