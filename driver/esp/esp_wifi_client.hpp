#pragma once

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_client.hpp"

namespace LibXR
{

class ESP32WifiClient : public WifiClient
{
 public:
  ESP32WifiClient();
  ~ESP32WifiClient() override;

  WifiError Enable() override;
  WifiError Disable() override;
  WifiError Connect(const Config& config) override;
  WifiError Disconnect() override;
  bool IsConnected() const override;
  const char* GetIPAddress() const override;
  WifiError Scan(std::vector<ScanResult>& results) override;
  int GetRSSI() const override;

 private:
  static void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                           void* event_data);

  static inline bool is_initialized_ = false;
  static inline esp_netif_t* netif_ = nullptr;

  bool enabled_ = false;
  bool connected_ = false;
  bool got_ip_ = false;
  char ip_str_[IP4ADDR_STRLEN_MAX] = {};

  LibXR::Semaphore semaphore_;
};

}  // namespace LibXR
