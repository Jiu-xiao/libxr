#include "esp_timebase.hpp"

#include "esp_attr.h"
#if SOC_SYSTIMER_SUPPORTED
#include "esp_private/systimer.h"
#endif

namespace LibXR
{

namespace
{
#if SOC_SYSTIMER_SUPPORTED
systimer_hal_context_t g_systimer_hal = {};
bool g_systimer_ready = false;
#endif
}  // namespace

ESP32Timebase::ESP32Timebase()
{
  ConfigureWrapRange(UINT64_MAX, UINT32_MAX);
#if SOC_SYSTIMER_SUPPORTED
  if (!g_systimer_ready)
  {
    systimer_hal_init(&g_systimer_hal);

    systimer_hal_tick_rate_ops_t tick_rate_ops = {
        .ticks_to_us = systimer_ticks_to_us,
        .us_to_ticks = systimer_us_to_ticks,
    };
    systimer_hal_set_tick_rate_ops(&g_systimer_hal, &tick_rate_ops);
    systimer_hal_enable_counter(&g_systimer_hal, SYSTIMER_COUNTER_ESPTIMER);
    g_systimer_ready = true;
  }
#endif
  SetReady();
}

MicrosecondTimestamp IRAM_ATTR Timebase::GetMicroseconds()
{
#if SOC_SYSTIMER_SUPPORTED
  if (g_systimer_ready)
  {
    return systimer_hal_get_time(&g_systimer_hal, SYSTIMER_COUNTER_ESPTIMER);
  }
#endif
  return static_cast<MicrosecondTimestamp>(esp_timer_get_time());
}

MillisecondTimestamp IRAM_ATTR Timebase::GetMilliseconds()
{
  return MillisecondTimestamp(static_cast<uint32_t>(GetMicroseconds() / 1000ULL));
}

}  // namespace LibXR
