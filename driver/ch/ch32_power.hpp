#pragma once

#include "power.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

/**
 * @brief CH32 平台电源管理基类实现 / CH32 power-manager base implementation
 */
class CH32PowerManager : public PowerManager
{
 public:
  CH32PowerManager() = default;

  void Reset() override;

  void Shutdown() override;
};

}  // namespace LibXR
