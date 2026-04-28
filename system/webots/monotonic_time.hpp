#pragma once

#include <time.h>

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

/**
 * @brief Webots 等待轮询片长（毫秒） / Webots timed-wait polling slice in milliseconds
 */
extern uint32_t _libxr_webots_poll_period_ms;  // NOLINT

namespace LibXR
{
namespace MonotonicTime
{

inline uint64_t NowMilliseconds()
{
  return _libxr_webots_time_count;
}

inline uint32_t RemainingMilliseconds(uint64_t deadline_ms)
{
  const uint64_t now_ms = NowMilliseconds();
  if (now_ms >= deadline_ms)
  {
    return 0;
  }
  return static_cast<uint32_t>(deadline_ms - now_ms);
}

inline uint32_t WaitSliceMilliseconds(uint32_t remaining_ms)
{
  if (remaining_ms == 0)
  {
    return 0;
  }

  const uint32_t poll_ms = _libxr_webots_poll_period_ms != 0 ? _libxr_webots_poll_period_ms : 1U;
  return remaining_ms < poll_ms ? remaining_ms : poll_ms;
}

inline timespec RealtimeDeadlineFromNow(uint32_t milliseconds)
{
  timespec ts = {};
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += static_cast<time_t>(milliseconds / 1000U);
  ts.tv_nsec += static_cast<long>(milliseconds % 1000U) * 1000000L;
  if (ts.tv_nsec >= 1000000000L)
  {
    ts.tv_sec += ts.tv_nsec / 1000000000L;
    ts.tv_nsec %= 1000000000L;
  }
  return ts;
}

}  // namespace MonotonicTime
}  // namespace LibXR
