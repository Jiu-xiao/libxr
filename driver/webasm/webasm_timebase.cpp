#include "webasm_timebase.hpp"

#include <chrono>

namespace LibXR
{
namespace
{
std::chrono::system_clock::time_point g_start_time;
}  // namespace

WebAsmTimebase::WebAsmTimebase()
{
  g_start_time = std::chrono::system_clock::now();
  ConfigureWrapRange(UINT64_MAX, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  const auto now = std::chrono::system_clock::now();
  const auto us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - g_start_time).count();
  return MicrosecondTimestamp(static_cast<uint64_t>(us));
}

MillisecondTimestamp Timebase::GetMilliseconds()
{
  const auto now = std::chrono::system_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_time).count();
  return MillisecondTimestamp(static_cast<uint32_t>(ms));
}
}  // namespace LibXR
