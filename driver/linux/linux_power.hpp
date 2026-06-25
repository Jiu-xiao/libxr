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
 * @brief Linux 电源管理实现 / Linux power manager implementation
 *
 * @note 使用系统命令执行重启和关机 / Uses system commands for reboot and shutdown
 */
class LinuxPowerManager : public PowerManager
{
 public:
  LinuxPowerManager() = default;

  /**
   * @brief 重启系统 / Reboot system
   */
  void Reset() override
  {
    CheckRoot();
    int ret = std::system("reboot");
    if (ret != 0)
    {
      throw std::runtime_error("Failed to reboot system");
    }
  }

  /**
   * @brief 关闭系统 / Power off system
   */
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
