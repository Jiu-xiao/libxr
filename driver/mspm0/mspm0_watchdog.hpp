#pragma once

#include "watchdog.hpp"

#if defined(__MSPM0_HAS_WWDT__)

#include <ti/driverlib/dl_wwdt.h>

namespace LibXR
{

class MSPM0Watchdog : public Watchdog
{
 public:
  explicit MSPM0Watchdog(WWDT_Regs* wwdt, uint32_t timeout_ms = 1000,
                         uint32_t feed_ms = 250, uint32_t clock = 32768U);

  ErrorCode SetConfig(const Configuration& config) override;

  ErrorCode Feed() override;

  ErrorCode Start() override;

  ErrorCode Stop() override;

 private:
  WWDT_Regs* wwdt_;
  uint32_t clock_;
  DL_WWDT_CLOCK_DIVIDE divider_;
  DL_WWDT_TIMER_PERIOD period_;
  bool initialized_;
};

}  // namespace LibXR

#endif
