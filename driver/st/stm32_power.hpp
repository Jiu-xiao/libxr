#pragma once

#include "libxr.hpp"
#include "main.h"
#include "power.hpp"

extern void __NVIC_SystemReset(void);

namespace LibXR
{

/**
 * @brief STM32 电源管理实现 / STM32 power manager implementation
 */
class STM32PowerManager : public PowerManager
{
 public:
  explicit STM32PowerManager();

  void Reset() override;

  void Shutdown() override;
};

}  // namespace LibXR
