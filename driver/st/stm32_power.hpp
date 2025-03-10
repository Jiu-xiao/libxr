#pragma once

#include "libxr.hpp"
#include "main.h"
#include "power.hpp"

extern void __NVIC_SystemReset(void);

namespace LibXR {

class STM32PowerManager : public PowerManager {
 public:
  explicit STM32PowerManager() {}

  void Reset() override { NVIC_SystemReset(); }

  void Shutdown() override { HAL_PWR_EnterSTANDBYMode(); }
};

}  // namespace LibXR
