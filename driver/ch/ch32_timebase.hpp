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

  static inline uint32_t cnt_per_microsec_;
};

}  // namespace LibXR
