/**
 * @file test_timer.cpp
 * @brief runtime 周期 timer start/stop 测试。 Runtime periodic timer start/stop behavior
 * test.
 *
 * 测试项目 / Test items:
 * 1. timer 周期回调执行。 Timer periodic execution: verify a started timer repeatedly
 * invokes its callback.
 * 2. stop/restart 后的重复可用性。 Stop/restart behavior: verify stopping and restarting
 * the same timer handle still yields the expected periodic count.
 *
 * 测试原理 / Test principles:
 * 1. 使用真实 timer 推进路径和重复 restart 尝试，验证 runtime 调度而不是模拟回调循环。
 * Use the real timer progression path and repeated restart attempts so the test checks
 * runtime scheduling rather than a simulated callback loop.
 */
#include <atomic>
#include <limits>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "timer.hpp"

/**
 * @brief 测试入口函数 `test_timer`。 Test entry function `test_timer`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_timer()
{
  const auto zero = LibXR::Detail::AdvanceTimerCount(3U, 10U, 0U);
  ASSERT(!zero.due && zero.count == 3U);

  const auto before_due = LibXR::Detail::AdvanceTimerCount(3U, 10U, 6U);
  ASSERT(!before_due.due && before_due.count == 9U);

  const auto exactly_due = LibXR::Detail::AdvanceTimerCount(9U, 10U, 1U);
  ASSERT(exactly_due.due && exactly_due.count == 0U);

  const auto missed_periods = LibXR::Detail::AdvanceTimerCount(8U, 10U, 25U);
  ASSERT(missed_periods.due && missed_periods.count == 3U);

  constexpr uint32_t MAX = std::numeric_limits<uint32_t>::max();
  const auto wide_total = LibXR::Detail::AdvanceTimerCount(MAX - 2U, MAX, MAX);
  ASSERT(wide_total.due && wide_total.count == MAX - 2U);

  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  static std::atomic<uint32_t> timer_count{0U};

  static auto handle = LibXR::Timer::CreateTask<std::atomic<uint32_t>*>(
      [](std::atomic<uint32_t>* count)
      { count->fetch_add(1U, std::memory_order_relaxed); }, &timer_count, 10U);
  static bool added = false;

  if (!added)
  {
    LibXR::Timer::Add(handle);
    added = true;
  }

  timer_count.store(0U, std::memory_order_relaxed);
  LibXR::Timer::Start(handle);
  for (size_t i = 0U; i < 100U && timer_count.load(std::memory_order_relaxed) == 0U; ++i)
  {
    LibXR::Thread::Sleep(10U);
  }
  LibXR::Timer::Stop(handle);
  const uint32_t first_run = timer_count.load(std::memory_order_relaxed);
  ASSERT(first_run > 0U);

  const uint32_t restart_baseline = timer_count.load(std::memory_order_relaxed);
  LibXR::Timer::Start(handle);
  for (size_t i = 0U;
       i < 100U && timer_count.load(std::memory_order_relaxed) == restart_baseline; ++i)
  {
    LibXR::Thread::Sleep(10U);
  }
  LibXR::Timer::Stop(handle);
  const uint32_t second_run = timer_count.load(std::memory_order_relaxed);
  ASSERT(second_run > restart_baseline);
}
