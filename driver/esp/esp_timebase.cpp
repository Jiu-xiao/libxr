#include "esp_timebase.hpp"

#include "esp_attr.h"
#if SOC_SYSTIMER_SUPPORTED
#include "esp_private/systimer.h"
#endif

namespace LibXR
{

ESP32Timebase::ESP32Timebase()
    : Timebase(static_cast<uint64_t>(UINT64_MAX), UINT32_MAX)
{
#if SOC_SYSTIMER_SUPPORTED
  systimer_hal_init(&systimer_hal_);

  systimer_hal_tick_rate_ops_t tick_rate_ops = {
      .ticks_to_us = systimer_ticks_to_us,
      .us_to_ticks = systimer_us_to_ticks,
  };
  systimer_hal_set_tick_rate_ops(&systimer_hal_, &tick_rate_ops);
  systimer_hal_enable_counter(&systimer_hal_, SYSTIMER_COUNTER_ESPTIMER);
  systimer_ready_ = true;
#endif
}

MicrosecondTimestamp IRAM_ATTR ESP32Timebase::_get_microseconds()
{
#if SOC_SYSTIMER_SUPPORTED
  if (systimer_ready_)
  {
    return systimer_hal_get_time(&systimer_hal_, SYSTIMER_COUNTER_ESPTIMER);
  }
#endif
  return static_cast<MicrosecondTimestamp>(esp_timer_get_time());
}

MillisecondTimestamp IRAM_ATTR ESP32Timebase::_get_milliseconds()
{
  return MillisecondTimestamp(static_cast<uint32_t>(_get_microseconds() / 1000ULL));
}

}  // namespace LibXR
