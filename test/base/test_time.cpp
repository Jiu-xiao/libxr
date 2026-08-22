/**
 * @file test_time.cpp
 * @brief `MicrosecondTimestamp` / `MillisecondTimestamp` 算术测试。
 * `MicrosecondTimestamp` and `MillisecondTimestamp` arithmetic tests.
 *
 * 测试项目 / Test items:
 * 1. 直线时间差与单位换算。 Straight-line subtraction: verify elapsed microsecond and
 * millisecond durations and their unit-conversion helpers.
 * 2. 单次回绕后的时间差计算。 Single-wrap subtraction: verify configured wrap ranges are
 * used when the new timestamp is numerically smaller than the old one.
 *
 * 测试原理 / Test principles:
 * 1. 同时检查原始 duration 和换算辅助接口，因为调用方会同时使用两者。 Check both raw
 * duration values and converted unit helpers, because callers use both surfaces.
 * 2. 在测试内主动改 wrap 配置，让回绕分支可确定复现。 Override the wrap configuration
 * in-test so the wraparound branch is exercised deterministically instead of depending on
 * platform timebase limits.
 */
#include <concepts>
#include <cstdint>
#include <type_traits>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

template <typename Left, typename Right>
concept EqualityComparable = requires(Left left, Right right) { left == right; };

template <typename Left, typename Right>
concept Subtractable = requires(Left left, Right right) { left - right; };

static_assert(sizeof(LibXR::ThreadTimestamp) == sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<LibXR::ThreadTimestamp>);
static_assert(!std::is_constructible_v<LibXR::ThreadTimestamp, uint32_t>);
static_assert(!std::is_convertible_v<LibXR::ThreadTimestamp, uint32_t>);
static_assert(
    !std::is_constructible_v<LibXR::ThreadTimestamp, LibXR::MillisecondTimestamp>);
static_assert(
    !std::is_constructible_v<LibXR::MillisecondTimestamp, LibXR::ThreadTimestamp>);
static_assert(
    !std::is_convertible_v<LibXR::ThreadTimestamp, LibXR::MillisecondTimestamp>);
static_assert(
    !std::is_convertible_v<LibXR::MillisecondTimestamp, LibXR::ThreadTimestamp>);
static_assert(!EqualityComparable<LibXR::ThreadTimestamp, LibXR::MillisecondTimestamp>);
static_assert(!Subtractable<LibXR::ThreadTimestamp, LibXR::MillisecondTimestamp>);
static_assert(std::same_as<decltype(LibXR::ThreadTimestamp().RawTicks()), uint32_t>);
static_assert(LibXR::Detail::SchedulerTicksUntil(UINT32_MAX - 2U, 1U) == 4U);
static_assert(LibXR::Detail::SchedulerTicksUntil(100U, 110U) == 10U);
static_assert(LibXR::Detail::SchedulerTicksUntil(110U, 100U) == 0U);
static_assert(LibXR::Detail::SchedulerTicksUntil(100U, 100U) == 0U);

struct TimebaseWrapGuard
{
  uint64_t old_us = LibXR::Detail::TimebaseMaxValidUs();
  uint32_t old_ms = LibXR::Detail::TimebaseMaxValidMs();

  ~TimebaseWrapGuard() { LibXR::Detail::ConfigureTimebaseWrapRange(old_us, old_ms); }
};

}  // namespace

/**
 * @brief 测试入口函数 `test_time`。 Test entry function `test_time`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_time()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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

  LibXR::Detail::ConfigureTimebaseWrapRange(UINT64_MAX, UINT32_MAX);
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
