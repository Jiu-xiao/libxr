#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_timebase()
{
  LibXR::MillisecondTimestamp start_ms(1000), end_ms(2005);
  LibXR::MicrosecondTimestamp start_us(1000), end_us(2005);
  const uint64_t old_max_valid_us = libxr_timebase_max_valid_us;
  const uint32_t old_max_valid_ms = libxr_timebase_max_valid_ms;

  start_ms = LibXR::Timebase::GetMilliseconds();
  start_us = LibXR::Timebase::GetMicroseconds();
  LibXR::Thread::Sleep(100);
  end_us = LibXR::Timebase::GetMicroseconds();
  end_ms = LibXR::Timebase::GetMilliseconds();

  ASSERT(std::fabs((end_ms - start_ms).ToMillisecond() - 100.0f) < 10);
  ASSERT(std::fabs((end_us - start_us).ToMicrosecond() - 100000.0f) < 10000);

  libxr_timebase_max_valid_ms = 999u;
  ASSERT((LibXR::MillisecondTimestamp(3u) - LibXR::MillisecondTimestamp(998u))
             .ToMillisecond() == 5u);

  libxr_timebase_max_valid_us = 999999u;
  ASSERT((LibXR::MicrosecondTimestamp(7u) - LibXR::MicrosecondTimestamp(999995u))
             .ToMicrosecond() == 12u);

  libxr_timebase_max_valid_us = old_max_valid_us;
  libxr_timebase_max_valid_ms = old_max_valid_ms;
}
