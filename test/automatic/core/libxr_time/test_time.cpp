/**
 * @file test_time.cpp
 * @brief 检查时间戳相减、单位换算和指定范围内的回绕。 /
 * Tests timestamp subtraction, unit conversion and configured wraparound.
 */

#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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
  TEST_ASSERT(static_cast<uint64_t>(us_elapsed) == 250);
  TEST_ASSERT(us_elapsed.ToMicrosecond() == 250);
  TEST_ASSERT(us_elapsed.ToMillisecond() == 0);
  TEST_ASSERT(equal(us_elapsed.ToSecond(), 0.00025));
  TEST_ASSERT(equal(us_elapsed.ToSecondf(), 0.00025f));

  LibXR::Detail::ConfigureTimebaseWrapRange(999, 999);
  const auto us_wrap = LibXR::MicrosecondTimestamp(3) - LibXR::MicrosecondTimestamp(998);
  TEST_ASSERT(static_cast<uint64_t>(us_wrap) == 5);
  TEST_ASSERT(us_wrap.ToMicrosecond() == 5);

  LibXR::Detail::ConfigureTimebaseWrapRange(UINT64_MAX, UINT32_MAX);
  const auto ms_elapsed =
      LibXR::MillisecondTimestamp(2500) - LibXR::MillisecondTimestamp(1000);
  TEST_ASSERT(static_cast<uint32_t>(ms_elapsed) == 1500);
  TEST_ASSERT(ms_elapsed.ToMillisecond() == 1500);
  TEST_ASSERT(ms_elapsed.ToMicrosecond() == 1500000);
  TEST_ASSERT(equal(ms_elapsed.ToSecond(), 1.5));
  TEST_ASSERT(equal(ms_elapsed.ToSecondf(), 1.5f));

  LibXR::Detail::ConfigureTimebaseWrapRange(UINT64_MAX, 99);
  const auto ms_wrap = LibXR::MillisecondTimestamp(2) - LibXR::MillisecondTimestamp(98);
  TEST_ASSERT(static_cast<uint32_t>(ms_wrap) == 4);
  TEST_ASSERT(ms_wrap.ToMillisecond() == 4);
  TEST_ASSERT(ms_wrap.ToMicrosecond() == 4000);
}
