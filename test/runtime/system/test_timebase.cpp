/**
 * @file test_timebase.cpp
 * @brief Runtime timebase progression and wrap-range configuration tests.
 *
 * Test items:
 * 1. Live progression: verify runtime microsecond and millisecond clocks advance by roughly the expected amount across a sleep interval.
 * 2. Configured wrap semantics: verify custom wrap ranges are applied to timestamp subtraction on both millisecond and microsecond scales.
 *
 * Test principle:
 * 1. Pair live time progression with explicit wrap-range overrides so the test covers both backend clock sourcing and static arithmetic policy.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
struct TimebaseWrapProbe : LibXR::Timebase
{
  static void Set(uint64_t max_valid_us, uint32_t max_valid_ms)
  {
    ConfigureWrapRange(max_valid_us, max_valid_ms);
  }

  static uint64_t GetUs() { return GetConfiguredWrapRangeUs(); }
  static uint32_t GetMs() { return GetConfiguredWrapRangeMs(); }

  LibXR::MicrosecondTimestamp _get_microseconds() override
  {
    ASSERT(false);
    return 0;
  }

  LibXR::MillisecondTimestamp _get_milliseconds() override
  {
    ASSERT(false);
    return 0;
  }
};
}  // namespace

void test_timebase()
{
  LibXR::MillisecondTimestamp start_ms(1000), end_ms(2005);
  LibXR::MicrosecondTimestamp start_us(1000), end_us(2005);
  const uint64_t old_max_valid_us = TimebaseWrapProbe::GetUs();
  const uint32_t old_max_valid_ms = TimebaseWrapProbe::GetMs();

  start_ms = LibXR::Timebase::GetMilliseconds();
  start_us = LibXR::Timebase::GetMicroseconds();
  LibXR::Thread::Sleep(100);
  end_us = LibXR::Timebase::GetMicroseconds();
  end_ms = LibXR::Timebase::GetMilliseconds();

  ASSERT(std::fabs((end_ms - start_ms).ToMillisecond() - 100.0f) < 10);
  ASSERT(std::fabs((end_us - start_us).ToMicrosecond() - 100000.0f) < 10000);

  TimebaseWrapProbe::Set(old_max_valid_us, 999u);
  ASSERT((LibXR::MillisecondTimestamp(3u) - LibXR::MillisecondTimestamp(998u))
             .ToMillisecond() == 5u);

  TimebaseWrapProbe::Set(999999u, 999u);
  ASSERT((LibXR::MicrosecondTimestamp(7u) - LibXR::MicrosecondTimestamp(999995u))
             .ToMicrosecond() == 12u);

  TimebaseWrapProbe::Set(old_max_valid_us, old_max_valid_ms);
}
