/**
 * @file test_timer.hpp
 * @brief 实际系统的 Timer 调度测试 / Timer scheduling test on the target system.
 *
 * 连续执行指定次数后自行停止，再观察是否出现额外回调；对象保留到系统复位。
 * Stop after the requested callback count and check for extra calls; keep the object
 * until system reset.
 */
#pragma once

#include <atomic>

#include "semaphore.hpp"
#include "test_assert.hpp"
#include "timer.hpp"

namespace LibXR::Test
{
/**
 * @brief 长期保留的定时器测试对象 / Caller-retained Timer test object.
 * @pre 在系统初始化后构造，地址保持不变，Run 只能调用一次。已注册任务不能删除。
 *      Construct after system initialization, keep its address stable, and call Run once.
 */
class TimerTest
{
 public:
  TimerTest() = default;
  TimerTest(const TimerTest&) = delete;
  TimerTest& operator=(const TimerTest&) = delete;

  /**
   * @brief 检查真实调度与回调内停止 / Check real scheduling and stopping from the
   * callback.
   * @param cycle_ms 定时器周期 / Timer period in milliseconds.
   * @param timeout_ms 每次回调进度的等待上限 / Deadline for each callback progress wait.
   * @param callbacks 回调次数 / Number of callbacks.
   */
  void Run(uint32_t cycle_ms = 10, uint32_t timeout_ms = 1000, uint32_t callbacks = 1000)
  {
    TEST_ASSERT(handle_ == nullptr);
    TEST_ASSERT(callbacks > 0);
    target_calls_ = callbacks;
    TEST_ASSERT(cycle_ms > 0 && cycle_ms <= timeout_ms / 8U);
    handle_ = Timer::CreateTask<TimerTest*>(
        [](TimerTest* self)
        {
          if (self->calls_.fetch_add(1, std::memory_order_relaxed) + 1 >=
              self->target_calls_)
          {
            Timer::Stop(self->handle_);
          }
          self->completed_.Post();
        },
        this, cycle_ms);
    // 在发布给调度线程前完成配置，之后只有回调修改启停状态。
    // Configure before publishing to the scheduler; only the callback changes enable
    // state afterwards.
    Timer::Start(handle_);
    Timer::Add(handle_);
    for (uint32_t received = 0; received < callbacks; ++received)
    {
      TEST_ASSERT(completed_.Wait(timeout_ms) == ErrorCode::OK);
      const uint32_t count = calls_.load(std::memory_order_relaxed);
      TEST_ASSERT(count >= received + 1U && count <= callbacks);
    }
    TEST_ASSERT(calls_.load(std::memory_order_relaxed) == callbacks);
    // 这是停止后的有限观察窗口，不把等待时长当成精确的回调次数。
    // Observe a bounded quiet interval; do not derive an exact callback count from
    // elapsed time.
    TEST_ASSERT(completed_.Wait(cycle_ms * 4U) == ErrorCode::TIMEOUT);
    TEST_ASSERT(calls_.load(std::memory_order_relaxed) == callbacks);
  }

 private:
  Timer::TimerHandle handle_ = nullptr;
  Semaphore completed_;
  std::atomic<uint32_t> calls_{0};
  uint32_t target_calls_ = 0;
};
}  // namespace LibXR::Test
