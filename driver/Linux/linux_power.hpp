#pragma once

#include <unistd.h>  // for geteuid()

#include <cstdlib>    // for system()
#include <stdexcept>  // for std::runtime_error

#include "libxr.hpp"
#include "logger.hpp"
#include "power.hpp"

namespace LibXR
{

/**
 * @class LinuxPowerManager
 * @brief 基于 Linux 的电源管理器实现 / Linux implementation of PowerManager
 *
 * 使用系统命令执行重启与关机操作，需运行权限支持。
 */
class LinuxPowerManager : public PowerManager
{
 public:
  LinuxPowerManager() = default;

  void Reset() override
  {
    CheckRoot();
    int ret = std::system("reboot");
    if (ret != 0)
    {
      throw std::runtime_error("Failed to reboot system");
    }
  }

  void Shutdown() override
  {
    CheckRoot();
    int ret = std::system("poweroff");
    if (ret != 0)
    {
      throw std::runtime_error("Failed to shut down system");
    }
  }

 private:
  void CheckRoot()
  {
    if (geteuid() != 0)
    {
      XR_LOG_WARN("Must run as root");
    }
  }
};

}  // namespace LibXR
