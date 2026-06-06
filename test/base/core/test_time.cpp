/**
 * @file test_time.cpp
 * @brief `MicrosecondTimestamp` and `MillisecondTimestamp` arithmetic tests.
 *
 * Test items:
 * 1. Straight-line subtraction: verify elapsed microsecond and millisecond durations and their unit-conversion helpers.
 * 2. Single-wrap subtraction: verify configured wrap ranges are used when the new timestamp is numerically smaller than the old one.
 *
 * Test principle:
 * 1. Check both raw duration values and converted unit helpers, because callers use both surfaces.
 * 2. Override the wrap configuration in-test so the wraparound branch is exercised deterministically instead of depending on platform timebase limits.
 */
#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

struct TimebaseWrapGuard
{
  uint64_t old_us = LibXR::Detail::TimebaseMaxValidUs();
  uint32_t old_ms = LibXR::Detail::TimebaseMaxValidMs();

  ~TimebaseWrapGuard() { LibXR::Detail::ConfigureTimebaseWrapRange(old_us, old_ms); }
};

}  // namespace

void test_time()
{
  TimebaseWrapGuard guard;

  const auto us_elapsed =
      LibXR::MicrosecondTimestamp(1250) - LibXR::MicrosecondTimestamp(1000);
  ASSERT(static_cast<uint64_t>(us_elapsed) == 250);
  ASSERT(us_elapsed.ToMicrosecond() == 250);
  ASSERT(us_elapsed.ToMillisecond() == 0);
  ASSERT(equal(us_elapsed.ToSecond(), 0.00025));
  ASSERT(equal(us_elapsed.ToSecondf(), 0.00025f));

  LibXR::Detail::ConfigureTimebaseWrapRange(999, 999);
  const auto us_wrap = LibXR::MicrosecondTimestamp(3) - LibXR::MicrosecondTimestamp(998);
  ASSERT(static_cast<uint64_t>(us_wrap) == 5);
  ASSERT(us_wrap.ToMicrosecond() == 5);

  const auto ms_elapsed =
      LibXR::MillisecondTimestamp(2500) - LibXR::MillisecondTimestamp(1000);
  ASSERT(static_cast<uint32_t>(ms_elapsed) == 1500);
  ASSERT(ms_elapsed.ToMillisecond() == 1500);
  ASSERT(ms_elapsed.ToMicrosecond() == 1500000);
  ASSERT(equal(ms_elapsed.ToSecond(), 1.5));
  ASSERT(equal(ms_elapsed.ToSecondf(), 1.5f));

  LibXR::Detail::ConfigureTimebaseWrapRange(UINT64_MAX, 99);
  const auto ms_wrap = LibXR::MillisecondTimestamp(2) - LibXR::MillisecondTimestamp(98);
  ASSERT(static_cast<uint32_t>(ms_wrap) == 4);
  ASSERT(ms_wrap.ToMillisecond() == 4);
  ASSERT(ms_wrap.ToMicrosecond() == 4000);
}
