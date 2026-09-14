/**
 * @file test_timebase.cpp
 * @brief Timebase 计时和回绕测试 / Timebase elapsed-time and wraparound tests.
 *
 * 在 Linux 上用参考时钟检查毫秒、微秒读数，并以固定输入检查回绕减法。
 * Check Linux millisecond/microsecond readings against a reference clock and exact
 * wraparound cases.
 */

#include <chrono>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
using ReferenceClock = std::chrono::steady_clock;

bool DurationWithinBounds(double measured_us, ReferenceClock::time_point start_before,
                          ReferenceClock::time_point start_after,
                          ReferenceClock::time_point end_before,
                          ReferenceClock::time_point end_after, double quantum_us)
{
  // 两次读取各被参考时钟夹住；内侧时间差是下限，外侧时间差是上限。
  // Each reading is bracketed by the reference clock; inner endpoints give the lower
  // bound, outer ones the upper.
  const double lower_us =
      std::chrono::duration<double, std::micro>(end_before - start_after).count();
  const double upper_us =
      std::chrono::duration<double, std::micro>(end_after - start_before).count();
  return measured_us >= lower_us - quantum_us && measured_us <= upper_us + quantum_us;
}

struct TimebaseWrapProbe : LibXR::Timebase
{
  static void Set(uint64_t max_valid_us, uint32_t max_valid_ms)
  {
    ConfigureWrapRange(max_valid_us, max_valid_ms);
  }

  static uint64_t GetUs() { return GetConfiguredWrapRangeUs(); }
  static uint32_t GetMs() { return GetConfiguredWrapRangeMs(); }
};
}  // namespace

void test_timebase()
{
  LibXR::MillisecondTimestamp start_ms(1000), end_ms(2005);
  LibXR::MicrosecondTimestamp start_us(1000), end_us(2005);
  const uint64_t old_max_valid_us = TimebaseWrapProbe::GetUs();
  const uint32_t old_max_valid_ms = TimebaseWrapProbe::GetMs();

  const auto start_ms_before = ReferenceClock::now();
  start_ms = LibXR::Timebase::GetMilliseconds();
  const auto start_ms_after = ReferenceClock::now();
  const auto start_us_before = ReferenceClock::now();
  start_us = LibXR::Timebase::GetMicroseconds();
  const auto start_us_after = ReferenceClock::now();
  // 只用 Sleep 拉开两次采样，实际间隔由参考时钟决定，不假设恰好过去 100 ms。
  // Sleep separates the samples; reference readings determine elapsed time, not an
  // assumed 100 ms.
  LibXR::Thread::Sleep(100);
  const auto end_us_before = ReferenceClock::now();
  end_us = LibXR::Timebase::GetMicroseconds();
  const auto end_us_after = ReferenceClock::now();
  const auto end_ms_before = ReferenceClock::now();
  end_ms = LibXR::Timebase::GetMilliseconds();
  const auto end_ms_after = ReferenceClock::now();

  // 两次带符号纳秒转微秒的截断误差相减小于 2 us；转成整数毫秒再增加至多 999 us。
  // Subtracting two signed ns-to-us truncations gives <2 us error; integer ms adds at
  // most 999 us.
  TEST_ASSERT(DurationWithinBounds((end_ms - start_ms).ToMillisecond() * 1000.0,
                                   start_ms_before, start_ms_after, end_ms_before,
                                   end_ms_after, 1001.0));
  TEST_ASSERT(DurationWithinBounds((end_us - start_us).ToMicrosecond(), start_us_before,
                                   start_us_after, end_us_before, end_us_after, 2.0));

  // 把毫秒最大值缩到 999，明确检查 998 -> 999 -> 0 -> 3 共经过五步。
  // Set the millisecond maximum to 999: 998 -> 999 -> 0 -> 3 must span five steps.
  TimebaseWrapProbe::Set(old_max_valid_us, 999u);
  TEST_ASSERT((LibXR::MillisecondTimestamp(3u) - LibXR::MillisecondTimestamp(998u))
                  .ToMillisecond() == 5u);

  TimebaseWrapProbe::Set(999999u, 999u);
  TEST_ASSERT((LibXR::MicrosecondTimestamp(7u) - LibXR::MicrosecondTimestamp(999995u))
                  .ToMicrosecond() == 12u);

  TimebaseWrapProbe::Set(old_max_valid_us, old_max_valid_ms);
}
