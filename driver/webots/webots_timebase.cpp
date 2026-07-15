#include "webots_timebase.hpp"

namespace LibXR
{
WebotsTimebase::WebotsTimebase()
{
  ConfigureWrapRange(UINT64_MAX, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  return _libxr_webots_time_count * 1000ULL;
}

MillisecondTimestamp Timebase::GetMilliseconds()
{
  return static_cast<uint32_t>(_libxr_webots_time_count);
}
}  // namespace LibXR
