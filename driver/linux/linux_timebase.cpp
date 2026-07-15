#include "linux_timebase.hpp"

namespace LibXR
{
namespace
{
int64_t GetElapsedMicroseconds()
{
  timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec - libxr_linux_start_time_spec.tv_sec) *
             1000000LL +
         static_cast<int64_t>(ts.tv_nsec - libxr_linux_start_time_spec.tv_nsec) / 1000LL;
}
}  // namespace

LinuxTimebase::LinuxTimebase()
{
  (void)clock_gettime(CLOCK_MONOTONIC, &libxr_linux_start_time_spec);
  ConfigureWrapRange(UINT64_MAX, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  return MicrosecondTimestamp(static_cast<uint64_t>(GetElapsedMicroseconds()));
}

MillisecondTimestamp Timebase::GetMilliseconds()
{
  return MillisecondTimestamp(static_cast<uint32_t>(GetElapsedMicroseconds() / 1000LL));
}
}  // namespace LibXR
