#include "hpm_timebase.hpp"

using namespace LibXR;

namespace
{
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
    : Timebase(static_cast<uint64_t>(UINT32_MAX) * 1000 + 999, UINT32_MAX),
      timer_(timer),
      clock_hz_(clock_get_frequency(clock))
{
}

MicrosecondTimestamp HPMTimebase::_get_microseconds()
{
  const uint64_t ticks = mchtmr_get_count(timer_);
  return MicrosecondTimestamp(ConvertTicksToTime(ticks, clock_hz_, 1000000ULL));
}

MillisecondTimestamp HPMTimebase::_get_milliseconds()
{
  const uint64_t ticks = mchtmr_get_count(timer_);
  return MillisecondTimestamp(ConvertTicksToTime(ticks, clock_hz_, 1000ULL));
}
