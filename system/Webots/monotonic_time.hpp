#pragma once

#include <time.h>

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

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
  UNUSED(remaining_ms);
  return 1;
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
