#pragma once

#include "main.h"
#include "timebase.hpp"

namespace LibXR {
class STM32Timebase : public Timebase {
 public:
  TimestampUS _get_microseconds() {
    uint32_t ms_old = HAL_GetTick();
    uint32_t tick_value_old = SysTick->VAL;
    uint32_t ms_new = HAL_GetTick();
    uint32_t tick_value_new = SysTick->VAL;

    auto time_diff = ms_new - ms_old;
    switch (time_diff) {
      case 0:
        return ms_new * 1000 + 1000 -
               tick_value_old * 1000 / (SysTick->LOAD + 1);
      case 1:
        /* Some interrupt happened between the two calls */
        return ms_new * 1000 + 1000 -
               tick_value_new * 1000 / (SysTick->LOAD + 1);
      default:
        /* Entering here means that some interrupt functions take more than a
         * millisecond. */
        ASSERT(false);
    }

    return 0;
  }

  TimestampMS _get_milliseconds() { return HAL_GetTick(); }
};
}  // namespace LibXR
