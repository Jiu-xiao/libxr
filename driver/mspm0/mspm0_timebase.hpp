#pragma once

#include "dl_systick.h"
#include "timebase.hpp"

namespace LibXR
{

class MSPM0Timebase : public Timebase
{
 public:
  MSPM0Timebase();

  static void OnSysTickInterrupt();

  static void Sync(uint32_t ticks);

  MicrosecondTimestamp _get_microseconds() override;

  MillisecondTimestamp _get_milliseconds() override;

  inline static volatile uint32_t sys_tick_ms;  // NOLINT
};

}  // namespace LibXR