#pragma once

#include "libxr_def.hpp"
#if defined(HAVE_WPA_CLIENT)

#include <NetworkManager.h>
#include <unistd.h>
#include <wpa_ctrl.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "logger.hpp"
#include "net/wifi_client.hpp"
#include "thread.hpp"

namespace LibXR
{

/**
 * @brief Linux Wi-Fi 客户端实现 / Linux Wi-Fi client implementation
 */
class LinuxWifiClient : public WifiClient
{
 public:
  /**
   * @brief 构造客户端对象 / Construct client object
   * @param ifname 网卡名，空则自动探测 / Interface name, auto-detect when null
   */
  LinuxWifiClient(const char* ifname = nullptr)
  {
    if (ifname)
    {
      strncpy(ifname_cstr_, ifname, sizeof(ifname_cstr_) - 1);
    }
    else
    {
      std::string iface = DetectWifiInterface();
      if (iface.empty())
      {
        XR_LOG_ERROR("Wi-Fi interface not found");
        ASSERT(false);
      }
      else
      {
        strncpy(ifname_cstr_, iface.c_str(), sizeof(ifname_cstr_) - 1);
      }
    }

    socket_path_ = "/var/run/wpa_supplicant/" + std::string(ifname_cstr_);
  }

  /**
   * @brief 析构函数 / Destructor
   */
  ~LinuxWifiClient() override
  {
    Disconnect();
    if (ctrl_)
    {
      wpa_ctrl_close(ctrl_);
    }
  }

  bool Enable() override
  {
    if (ctrl_)
    {
      return true;
    }
    ctrl_ = wpa_ctrl_open(socket_path_.c_str());
    if (ctrl_)
    {
      XR_LOG_PASS("Wi-Fi enabled: %s", socket_path_.c_str());
      return true;
    }
    XR_LOG_ERROR("Wi-Fi enable failed: %s", socket_path_.c_str());
    return false;
  }

  void Disable() override
  {
    Disconnect();
    if (ctrl_)
    {
      wpa_ctrl_close(ctrl_);
      ctrl_ = nullptr;
    }
  }

  bool IsConnected() const override
  {
    std::string out;
    if (!SendCommand("STATUS", out))
    {
      return false;
    }
    return out.find("wpa_state=COMPLETED") != std::string::npos;
  }

  IPAddressRaw GetIPAddress() const override
  {
    char cmd[128];
    auto ans = snprintf(cmd, sizeof(cmd), "ip -4 addr show %s | grep inet", ifname_cstr_);
    UNUSED(ans);
    FILE* fp = popen(cmd, "r");
    if (!fp)
    {
      return {};
    }

    char buf[64] = {};
    IPAddressRaw ip = {};
    if (fgets(buf, sizeof(buf), fp))
    {
      char ip_str[32] = {};
      auto ret = sscanf(buf, " inet %[^/]", ip_str);
      UNUSED(ret);
      ip = IPAddressRaw::FromString(ip_str);
    }
    pclose(fp);
    return ip;
  }

  MACAddressRaw GetMACAddress() const override
  {
    char path[128];
    auto ans = snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname_cstr_);
    UNUSED(ans);
    FILE* fp = fopen(path, "r");
    if (!fp)
    {
      return {};
    }

    char mac_str[32] = {};
    if (!fgets(mac_str, sizeof(mac_str), fp))
    {
      auto ans = fclose(fp);
      UNUSED(ans);
      return {};
    }
    auto ret = fclose(fp);
    UNUSED(ret);
    mac_str[strcspn(mac_str, "\n")] = 0;
    return MACAddressRaw::FromString(mac_str);
  }

  WifiError Connect(const Config& config) override
  {
    std::string out;
    SendCommand("REMOVE_NETWORK all", out);
    SendCommand("ADD_NETWORK", out);
    int netid = atoi(out.c_str());
    if (netid < 0)
    {
      XR_LOG_ERROR("ADD_NETWORK failed: %s", out.c_str());
      return WifiError::HARDWARE_FAILURE;
    }

    std::stringstream cmd;
    cmd << "SET_NETWORK " << netid << " ssid \"" << config.ssid << "\"";
    if (!SendCommand(cmd.str().c_str(), out) || out.find("OK") == std::string::npos)
    {
      XR_LOG_ERROR("SET_NETWORK ssid failed: %s", out.c_str());
      return WifiError::AUTHENTICATION_FAILED;
    }

    cmd.str("");
    cmd << "SET_NETWORK " << netid << " psk \"" << config.password << "\"";
    if (!SendCommand(cmd.str().c_str(), out) || out.find("OK") == std::string::npos)
    {
      XR_LOG_ERROR("SET_NETWORK psk failed: %s", out.c_str());
      return WifiError::AUTHENTICATION_FAILED;
    }

    std::vector<std::string> cleanup_cmds = {
        "key_mgmt WPA-PSK", "eap NONE", "phase1 \"\"", "identity \"\"", "password \"\"",
    };
    for (const auto& line : cleanup_cmds)
    {
      cmd.str("");
      cmd << "SET_NETWORK " << netid << " " << line;
      SendCommand(cmd.str().c_str(), out);
    }

    cmd.str("");
    cmd << "ENABLE_NETWORK " << netid;
    if (!SendCommand(cmd.str().c_str(), out) || out.find("OK") == std::string::npos)
    {
      XR_LOG_ERROR("ENABLE_NETWORK failed: %s", out.c_str());
      return WifiError::HARDWARE_FAILURE;
    }

    cmd.str("");
    cmd << "SELECT_NETWORK " << netid;
    SendCommand(cmd.str().c_str(), out);

    const int TIMEOUT_MS = 30000;
    const int INTERVAL_MS = 300;
    int elapsed = 0;

    while (elapsed < TIMEOUT_MS)
    {
      std::string status;
      SendCommand("STATUS", status);

      if (status.find("wpa_state=COMPLETED") != std::string::npos)
      {
        XR_LOG_PASS("Wi-Fi Connected to SSID: %s", config.ssid);
        return WifiError::NONE;
      }

      if (status.find("wpa_state=INACTIVE") != std::string::npos)
      {
        XR_LOG_ERROR("Wi-Fi Connection failed: %s", status.c_str());
        return WifiError::AUTHENTICATION_FAILED;
      }

      LibXR::Thread::Sleep(INTERVAL_MS);
      elapsed += INTERVAL_MS;
    }

    XR_LOG_ERROR("Wi-Fi Connection timeout");
    return WifiError::CONNECTION_TIMEOUT;
  }

  WifiError Disconnect() override
  {
    std::string out;
    SendCommand("DISCONNECT", out);
    SendCommand("REMOVE_NETWORK all", out);
    if (IsConnected())
    {
      return WifiError::UNKNOWN;
    }
    return WifiError::NONE;
  }

  WifiError Scan(ScanResult* out_list, size_t max_count, size_t& out_found) override
  {
    std::string out;
    SendCommand("SCAN", out);
    sleep(2);
    SendCommand("SCAN_RESULTS", out);

    std::istringstream ss(out);
    std::string line;
    out_found = 0;
    std::getline(ss, line);
    while (std::getline(ss, line) && out_found < max_count)
    {
      std::istringstream ls(line);
      std::string bssid, freq, signal, flags, ssid;
      ls >> bssid >> freq >> signal >> flags;
      std::getline(ls, ssid);
      if (!ssid.empty() && ssid[0] == '\t')
      {
        ssid.erase(0, 1);
      }

      auto& r = out_list[out_found++];
      strncpy(r.ssid, ssid.c_str(), sizeof(r.ssid) - 1);
      r.rssi = atoi(signal.c_str());
      r.security =
          (flags.find("WPA2") != std::string::npos) ? Security::WPA2_PSK : Security::OPEN;
    }

    return WifiError::NONE;
  }

  int GetRSSI() const override
  {
    std::string out;
    SendCommand("SIGNAL_POLL", out);
    auto pos = out.find("RSSI=");
    return pos != std::string::npos ? atoi(out.c_str() + pos + 5) : 0;
  }

 private:
  bool SendCommand(const char* cmd, std::string& result) const
  {
    if (!ctrl_)
    {
      return false;
    }
    char buf[4096];
    size_t len = sizeof(buf);
    int ret = wpa_ctrl_request(ctrl_, cmd, strlen(cmd), buf, &len, nullptr);
    if (ret == 0)
    {
      result.assign(buf, len);
      return true;
    }
    XR_LOG_ERROR("wpa_ctrl_request failed: %s\n", strerror(errno));
    return false;
  }

  std::string DetectWifiInterface()
  {
    GError* error = nullptr;
    NMClient* client = nm_client_new(nullptr, &error);
    if (!client)
    {
      auto ans = fprintf(stderr, "Failed to create NMClient: %s\n",
                         error ? error->message : "unknown error");
      UNUSED(ans);

      if (error)
      {
        g_error_free(error);
      }
      return "";
    }

    const GPtrArray* devices = nm_client_get_devices(client);
    for (guint i = 0; i < devices->len; ++i)
    {
      NMDevice* dev = (NMDevice*)g_ptr_array_index(devices, i);
      if (NM_IS_DEVICE_WIFI(dev))
      {
        const char* ifname = nm_device_get_iface(dev);
        if (ifname)
        {
          std::string iface_str(ifname);
          g_object_unref(client);
          return iface_str;
        }
      }
    }

    g_object_unref(client);
    return "";
  }

  char ifname_cstr_[32] = {};  // 静态存储接口名，避免动态分配
  std::string socket_path_;
  wpa_ctrl* ctrl_ = nullptr;
};

}  // namespace LibXR

#endif
