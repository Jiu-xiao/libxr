#include "hpm_timebase.hpp"

#include "driver_async_condition.hpp"
#if defined(LIBXR_SYSTEM_freertos)
#include "FreeRTOS.h"
#endif

using namespace LibXR;

namespace
{
MCHTMR_Type* g_timer = HPM_MCHTMR;
uint32_t g_clock_hz = 0u;

static inline uint64_t ConvertTicksToTime(uint64_t ticks, uint32_t freq_hz,
                                          uint64_t scale)
{
  if (freq_hz == 0u) return 0u;

  const uint64_t whole = ticks / freq_hz;
  const uint64_t rem = ticks - (whole * freq_hz);
  const uint64_t frac = (rem * scale) / freq_hz;
  return whole * scale + frac;
}
}  // namespace

HPMTimebase::HPMTimebase(MCHTMR_Type* timer, clock_name_t clock)
{
  g_timer = timer;
  g_clock_hz = clock_get_frequency(clock);
  ConfigureWrapRange(static_cast<uint64_t>(UINT32_MAX) * 1000ULL + 999ULL, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  const uint64_t ticks = mchtmr_get_count(g_timer);
  return MicrosecondTimestamp(ConvertTicksToTime(ticks, g_clock_hz, 1000000ULL));
}

MillisecondTimestamp Timebase::GetMilliseconds()
{
  const uint64_t ticks = mchtmr_get_count(g_timer);
  return MillisecondTimestamp(ConvertTicksToTime(ticks, g_clock_hz, 1000ULL));
}

#if defined(LIBXR_SYSTEM_freertos)
static_assert(
    configUSE_TICK_HOOK == 1,
    "LibXR drives its DriverAsyncCondition waits from the FreeRTOS tick hook: set "
    "configUSE_TICK_HOOK to 1 in FreeRTOSConfig.h");

// FreeRTOS 节拍（LibXR 要求 1 kHz）推进驱动的条件等待。
// The FreeRTOS tick (1 kHz, which LibXR requires) advances the drivers' condition waits.
extern "C" void vApplicationTickHook(void)  // NOLINT
{
  DriverAsyncCondition::OnTick();
}
#endif
