#include "esp_wifi_client.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace LibXR
{
namespace
{
constexpr uint32_t kWifiConnectTimeoutMs = 15000U;
constexpr uint32_t kWifiDhcpTimeoutMs = 15000U;
constexpr uint32_t kWifiDisconnectTimeoutMs = 5000U;
constexpr uint16_t kMaxScanResults = 20U;

size_t BoundedStringLength(const char* text, size_t max_len)
{
  if (text == nullptr) return 0;
  size_t len = 0;
  while (len < max_len && text[len] != '\0')
  {
    ++len;
  }
  return len;
}

void CopyToWifiField(uint8_t* dst, size_t dst_size, const char* src)
{
  if (dst_size == 0) return;
  const size_t copy_len = BoundedStringLength(src, dst_size - 1);
  if (copy_len > 0)
  {
    std::memcpy(dst, src, copy_len);
  }
  dst[copy_len] = 0;
}

void CopyToCharField(char* dst, size_t dst_size, const char* src)
{
  if (dst_size == 0) return;
  const size_t copy_len = BoundedStringLength(src, dst_size - 1);
  if (copy_len > 0)
  {
    std::memcpy(dst, src, copy_len);
  }
  dst[copy_len] = '\0';
}

}  // namespace

ESP32WifiClient::ESP32WifiClient()
{
  if (is_initialized_)
  {
    init_ok_ = true;
    return;
  }

  esp_err_t err = nvs_flash_init();
  if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
  {
    if (nvs_flash_erase() != ESP_OK)
    {
      return;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
  {
    return;
  }

  err = esp_netif_init();
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
  {
    return;
  }

  err = esp_event_loop_create_default();
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
  {
    return;
  }

  netif_ = esp_netif_create_default_wifi_sta();
  if (netif_ == nullptr)
  {
    return;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
  {
    return;
  }

  is_initialized_ = true;
  init_ok_ = true;
}

ESP32WifiClient::~ESP32WifiClient()
{
  if (enabled_)
  {
    Disable();
  }

  UnregisterHandlers();
}

bool ESP32WifiClient::Enable()
{
  if (enabled_) return true;

  if (!init_ok_)
  {
    return false;
  }

  if (!RegisterHandlers())
  {
    return false;
  }

  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
  {
    UnregisterHandlers();
    return false;
  }

  ResetConnectionState();
  DrainEvents();
  if (esp_wifi_start() != ESP_OK)
  {
    UnregisterHandlers();
    return false;
  }
  enabled_ = true;

  wifi_config_t cfg = {};
  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
  if (err == ESP_OK && cfg.sta.ssid[0] != 0)
  {
    ResetConnectionState();
    DrainEvents();
    if (esp_wifi_connect() == ESP_OK)
    {
      if (semaphore_.Wait(kWifiConnectTimeoutMs) == ErrorCode::OK && connected_)
      {
        (void)semaphore_.Wait(kWifiDhcpTimeoutMs);
      }
    }
  }

  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
  {
    connected_ = true;
    got_ip_ = true;

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif_, &ip_info) == ESP_OK)
    {
      esp_ip4addr_ntoa(&ip_info.ip, ip_str_, sizeof(ip_str_));
    }
  }

  return true;
}

void ESP32WifiClient::Disable()
{
  if (!enabled_) return;

  (void)esp_wifi_stop();
  enabled_ = false;
  ResetConnectionState();
  UnregisterHandlers();
}

WifiClient::WifiError ESP32WifiClient::Connect(const Config& config)
{
  if (!enabled_) return WifiError::NOT_ENABLED;

  ResetConnectionState();
  DrainEvents();

  wifi_config_t wifi_config{};
  CopyToWifiField(wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), config.ssid);
  CopyToWifiField(wifi_config.sta.password, sizeof(wifi_config.sta.password),
                  config.password);
  if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK)
  {
    return WifiError::INVALID_CONFIG;
  }
  if (esp_wifi_connect() != ESP_OK)
  {
    return WifiError::HARDWARE_FAILURE;
  }

  if (semaphore_.Wait(kWifiConnectTimeoutMs) != ErrorCode::OK)
  {
    return WifiError::CONNECTION_TIMEOUT;
  }
  if (!connected_) return WifiError::CONNECTION_TIMEOUT;

  if (semaphore_.Wait(kWifiDhcpTimeoutMs) != ErrorCode::OK)
  {
    return WifiError::DHCP_FAILED;
  }
  if (!got_ip_) return WifiError::DHCP_FAILED;

  return WifiError::NONE;
}

WifiClient::WifiError ESP32WifiClient::Disconnect()
{
  if (!enabled_) return WifiError::NOT_ENABLED;
  if (!connected_) return WifiError::NONE;

  DrainEvents();
  if (esp_wifi_disconnect() != ESP_OK)
  {
    return WifiError::HARDWARE_FAILURE;
  }

  if (semaphore_.Wait(kWifiDisconnectTimeoutMs) != ErrorCode::OK)
  {
    return WifiError::UNKNOWN;
  }

  return WifiError::NONE;
}

bool ESP32WifiClient::IsConnected() const { return connected_; }

IPAddressRaw ESP32WifiClient::GetIPAddress() const
{
  return IPAddressRaw::FromString(ip_str_);
}

MACAddressRaw ESP32WifiClient::GetMACAddress() const
{
  uint8_t mac[6] = {};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  MACAddressRaw result;
  std::memcpy(result.bytes, mac, 6);
  return result;
}

WifiClient::WifiError ESP32WifiClient::Scan(ScanResult* out_list, size_t max_count,
                                            size_t& out_found)
{
  out_found = 0;
  if (!enabled_)
  {
    return WifiError::NOT_ENABLED;
  }
  if ((out_list == nullptr) && (max_count != 0U))
  {
    return WifiError::INVALID_CONFIG;
  }

  wifi_scan_config_t scan_config = {};
  if (esp_wifi_scan_start(&scan_config, true) != ESP_OK)
  {
    return WifiError::SCAN_FAILED;
  }

  uint16_t ap_num = 0;
  if (esp_wifi_scan_get_ap_num(&ap_num) != ESP_OK)
  {
    return WifiError::SCAN_FAILED;
  }

  uint16_t copy_count = ap_num;
  if (copy_count > max_count)
  {
    copy_count = static_cast<uint16_t>(max_count);
  }
  if (copy_count > kMaxScanResults)
  {
    copy_count = kMaxScanResults;
  }

  wifi_ap_record_t ap_records[kMaxScanResults] = {};
  if ((copy_count > 0U) && (esp_wifi_scan_get_ap_records(&copy_count, ap_records) != ESP_OK))
  {
    return WifiError::SCAN_FAILED;
  }

  out_found = copy_count;
  for (uint16_t i = 0; i < copy_count; ++i)
  {
    CopyToCharField(out_list[i].ssid, sizeof(out_list[i].ssid),
                    reinterpret_cast<const char*>(ap_records[i].ssid));
    out_list[i].rssi = ap_records[i].rssi;
    out_list[i].security = (ap_records[i].authmode == WIFI_AUTH_OPEN) ? Security::OPEN
                           : (ap_records[i].authmode == WIFI_AUTH_WPA2_PSK)
                               ? Security::WPA2_PSK
                               : Security::UNKNOWN;
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
        self->got_ip_ = false;
        self->ip_str_[0] = '\0';
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
        self->got_ip_ = false;
        self->ip_str_[0] = '\0';
        self->semaphore_.Post();
        break;
      default:
        break;
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

bool ESP32WifiClient::RegisterHandlers()
{
  if (handlers_registered_)
  {
    return true;
  }

  if ((esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                           &EventHandler, this,
                                           &wifi_connected_handler_) != ESP_OK) ||
      (esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                           &EventHandler, this,
                                           &wifi_disconnected_handler_) != ESP_OK) ||
      (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &EventHandler,
                                           this, &got_ip_handler_) != ESP_OK) ||
      (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &EventHandler,
                                           this, &lost_ip_handler_) != ESP_OK))
  {
    UnregisterHandlers();
    return false;
  }

  handlers_registered_ = true;
  return true;
}

void ESP32WifiClient::UnregisterHandlers()
{
  if ((wifi_connected_handler_ == nullptr) && (wifi_disconnected_handler_ == nullptr) &&
      (got_ip_handler_ == nullptr) && (lost_ip_handler_ == nullptr))
  {
    handlers_registered_ = false;
    return;
  }

  if (wifi_connected_handler_ != nullptr)
  {
    (void)esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                                wifi_connected_handler_);
    wifi_connected_handler_ = nullptr;
  }
  if (wifi_disconnected_handler_ != nullptr)
  {
    (void)esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                wifi_disconnected_handler_);
    wifi_disconnected_handler_ = nullptr;
  }
  if (got_ip_handler_ != nullptr)
  {
    (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                got_ip_handler_);
    got_ip_handler_ = nullptr;
  }
  if (lost_ip_handler_ != nullptr)
  {
    (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                lost_ip_handler_);
    lost_ip_handler_ = nullptr;
  }
  handlers_registered_ = false;
}

void ESP32WifiClient::ResetConnectionState()
{
  connected_ = false;
  got_ip_ = false;
  ip_str_[0] = '\0';
}

void ESP32WifiClient::DrainEvents()
{
  while (semaphore_.Wait(0) == ErrorCode::OK)
  {
  }
}

}  // namespace LibXR
