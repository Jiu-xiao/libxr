#include "ch32_power.hpp"

using namespace LibXR;

// CH32 reset / standby hooks are thin wrappers over the vendor library entry points.
// CH32 的复位 / 待机接口只是对标准库入口做薄封装。
void CH32PowerManager::Reset() { NVIC_SystemReset(); }

void CH32PowerManager::Shutdown()
{
#if defined(PWR_EnterSTANDBYMode)
  PWR_EnterSTANDBYMode();
#elif defined(__CH32H417_H)
  // CH32H417 stdlib currently exposes STOP but not STANDBY entry points.
  // CH32H417 的标准库当前只暴露 STOP，没有公开 STANDBY 入口。
  PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
#else
#error "No CH32 low-power shutdown entry point is available for this target."
#endif
}
