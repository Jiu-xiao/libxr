#pragma once

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

extern uint32_t SystemCoreClock;

namespace LibXR
{

class CH32Timebase : public Timebase
{
 public:
  CH32Timebase() : Timebase(UINT64_MAX, UINT32_MAX)
  {
    cnt_per_microsec_ = SystemCoreClock / 1000000;
  }

  TimestampUS _get_microseconds() override
  {
    uint64_t cnt = SysTick->CNT;
    return TimestampUS(cnt / cnt_per_microsec_);
  }

  TimestampMS _get_milliseconds() override { return cnt_per_microsec_ / 1000; }

  static inline uint32_t cnt_per_microsec_;
};

}  // namespace LibXR
