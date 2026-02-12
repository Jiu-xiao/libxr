#pragma once

#include "libxr.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

extern uint32_t SystemCoreClock;

namespace LibXR
{

class CH32Timebase : public Timebase
{
 public:
  CH32Timebase();

  MicrosecondTimestamp _get_microseconds() override;

  MillisecondTimestamp _get_milliseconds() override;

  static inline void OnSysTickInterrupt();

  void Sync(uint32_t ticks);

  static inline volatile uint32_t sys_tick_ms_ = 0;
};

}  // namespace LibXR
