#include "ch32_timebase.hpp"

using namespace LibXR;

CH32Timebase::CH32Timebase() : Timebase(UINT64_MAX, UINT32_MAX)
{
  cnt_per_microsec_ = SystemCoreClock / 1000000;
}

MicrosecondTimestamp CH32Timebase::_get_microseconds()
{
  uint64_t cnt = SysTick->CNT;
  return MicrosecondTimestamp(cnt / cnt_per_microsec_);
}

MillisecondTimestamp CH32Timebase::_get_milliseconds()
{
  return cnt_per_microsec_ / 1000;
}
