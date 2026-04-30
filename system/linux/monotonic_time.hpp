#pragma once

#include <time.h>

#include <cstdint>

extern struct timespec libxr_linux_start_time_spec;

namespace LibXR
{
namespace MonotonicTime
{
inline uint64_t SpecMicroseconds(const timespec& ts)
{
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

inline timespec NowSpec()
{
  timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts;
}

inline uint64_t NowMicroseconds()
{
  return SpecMicroseconds(NowSpec());
}

inline uint64_t NowMilliseconds()
{
  return NowMicroseconds() / 1000ULL;
}

inline uint64_t XrToSharedMicroseconds(uint64_t timestamp_us)
{
  const uint64_t start_us = SpecMicroseconds(libxr_linux_start_time_spec);
  return UINT64_MAX - start_us < timestamp_us ? UINT64_MAX : start_us + timestamp_us;
}

inline uint64_t SharedToXrMicroseconds(uint64_t timestamp_us)
{
  const uint64_t start_us = SpecMicroseconds(libxr_linux_start_time_spec);
  return timestamp_us >= start_us ? timestamp_us - start_us : 0;
}

inline timespec RelativeFromMilliseconds(uint32_t milliseconds)
{
  timespec ts = {};
  ts.tv_sec = static_cast<time_t>(milliseconds / 1000U);
  ts.tv_nsec = static_cast<long>(milliseconds % 1000U) * 1000000L;
  return ts;
}

inline timespec AddMilliseconds(timespec base, uint64_t milliseconds)
{
  base.tv_sec += static_cast<time_t>(milliseconds / 1000ULL);
  base.tv_nsec += static_cast<long>(milliseconds % 1000ULL) * 1000000L;
  if (base.tv_nsec >= 1000000000L)
  {
    base.tv_sec += base.tv_nsec / 1000000000L;
    base.tv_nsec %= 1000000000L;
  }
  return base;
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

inline int64_t ElapsedMicroseconds(const timespec& start)
{
  const timespec now = NowSpec();
  return static_cast<int64_t>(now.tv_sec - start.tv_sec) * 1000000LL +
         static_cast<int64_t>(now.tv_nsec - start.tv_nsec) / 1000LL;
}

inline uint32_t WaitSliceMilliseconds(uint32_t remaining_ms)
{
  return remaining_ms;
}

}  // namespace MonotonicTime
}  // namespace LibXR
