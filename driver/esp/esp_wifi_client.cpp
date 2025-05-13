#include "esp_wifi_client.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace LibXR
{

ESP32WifiClient::ESP32WifiClient()
{
  if (!is_initialized_)
  {
    esp_netif_init();
    esp_event_loop_create_default();
    netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    is_initialized_ = true;
  }
}

ESP32WifiClient::~ESP32WifiClient()
{
  if (enabled_)
  {
    Disable();
  }
}

WifiClient::WifiError ESP32WifiClient::Enable()
{
  if (enabled_) return WifiError::ALREADY_ENABLED;

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  enabled_ = true;

  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &EventHandler, this);
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &EventHandler,
                             this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &EventHandler, this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &EventHandler, this);
  return WifiError::NONE;
}

WifiClient::WifiError ESP32WifiClient::Disable()
{
  if (!enabled_) return WifiError::NOT_ENABLED;

  esp_wifi_stop();
  enabled_ = false;
  connected_ = false;
  return WifiError::NONE;
}

WifiClient::WifiError ESP32WifiClient::Connect(const Config& config)
{
  if (!enabled_) return WifiError::NOT_ENABLED;

  wifi_config_t wifi_config{};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), config.ssid.c_str(),
               sizeof(wifi_config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), config.password.c_str(),
               sizeof(wifi_config.sta.password));
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_connect();

  while (semaphore_.Wait(0) == ErrorCode::OK)
  {
  }

  semaphore_.Wait();
  if (!connected_)
  {
    return WifiError::CONNECTION_TIMEOUT;
  }

  semaphore_.Wait();
  if (!got_ip_)
  {
    return WifiError::DHCP_FAILED;
  }

  return WifiError::NONE;
}

WifiClient::WifiError ESP32WifiClient::Disconnect()
{
  if (!enabled_)
  {
    return WifiError::NOT_ENABLED;
  }
  if (!connected_)
  {
    return WifiError::NONE;
  }
  while (semaphore_.Wait(0) == ErrorCode::OK)
  {
  }
  esp_wifi_disconnect();
  semaphore_.Wait();
  return WifiError::NONE;
}

bool ESP32WifiClient::IsConnected() const { return connected_; }

const char* ESP32WifiClient::GetIPAddress() const { return ip_str_; }

WifiClient::WifiError ESP32WifiClient::Scan(std::vector<ScanResult>& results)
{
  wifi_scan_config_t scan_config = {};
  if (esp_wifi_scan_start(&scan_config, true) != ESP_OK)
  {
    return WifiError::SCAN_FAILED;
  }

  uint16_t ap_num = 0;
  esp_wifi_scan_get_ap_num(&ap_num);

  std::vector<wifi_ap_record_t> ap_records(ap_num);
  if (esp_wifi_scan_get_ap_records(&ap_num, ap_records.data()) != ESP_OK)
  {
    return WifiError::SCAN_FAILED;
  }

  results.clear();
  for (const auto& record : ap_records)
  {
    ScanResult r;
    r.ssid = reinterpret_cast<const char*>(record.ssid);
    r.rssi = record.rssi;
    r.security = (record.authmode == WIFI_AUTH_OPEN)       ? Security::OPEN
                 : (record.authmode == WIFI_AUTH_WPA2_PSK) ? Security::WPA2_PSK
                                                           : Security::UNKNOWN;
    results.push_back(r);
  }

  return WifiError::NONE;
}

void ESP32WifiClient::EventHandler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data)
{
  auto* self = static_cast<ESP32WifiClient*>(arg);

  if (event_base == WIFI_EVENT)
  {
    switch (event_id)
    {
      case WIFI_EVENT_STA_CONNECTED:
        self->connected_ = true;
        self->semaphore_.Post();
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        self->connected_ = false;
        self->semaphore_.Post();
        break;
      default:
        break;
    }
  }

  if (event_base == IP_EVENT)
  {
    switch (event_id)
    {
      case IP_EVENT_STA_GOT_IP:
      {
        ip_event_got_ip_t* event = reinterpret_cast<ip_event_got_ip_t*>(event_data);
        self->got_ip_ = true;
        esp_ip4addr_ntoa(&event->ip_info.ip, self->ip_str_, sizeof(self->ip_str_));
        self->semaphore_.Post();
        break;
      }
      case IP_EVENT_STA_LOST_IP:
      {
        self->got_ip_ = false;
        break;
      }
    }
  }
}

int ESP32WifiClient::GetRSSI() const
{
  if (!connected_) return -127;

  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
  {
    return ap_info.rssi;
  }
  return -127;
}

}  // namespace LibXR
