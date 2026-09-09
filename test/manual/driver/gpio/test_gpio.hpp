/**
 * @file test_gpio.hpp
 * @brief GPIO 接线回环与边沿中断测试 / Wired GPIO loopback and edge interrupt tests.
 *
 * 用两个专用引脚检查高低电平、指定边沿的回调次数、回调上下文和中断禁用。
 * Check levels, selected edge counts, callback context and interrupt disabling using
 * two dedicated pins wired together. Keep the interrupt test object until reset.
 */
#pragma once

#include <atomic>

#include "gpio.hpp"
#include "test_assert.hpp"
#include "thread.hpp"

namespace LibXR::Test
{
namespace Detail
{
inline void WaitForGPIOLevel(GPIO& input, bool level, uint32_t settle_ms,
                             uint32_t timeout_ms)
{
  const uint32_t start = Thread::GetTime();
  while (input.Read() != level)
  {
    TEST_ASSERT(Thread::GetTime() - start < timeout_ms);
    Thread::Sleep(1);
  }
  // 到达目标电平后再观察一次，避免只检查到短暂跳变。
  // Check again after settling rather than accepting only a transient level.
  Thread::Sleep(settle_ms);
  TEST_ASSERT(input.Read() == level);
}
}  // namespace Detail

/**
 * @brief 反复检查推挽输出到输入的接线回环 / Repeatedly check push-pull loopback.
 * @pre 两个不同的物理引脚已连接且专供测试，输入中断未启用。从普通任务调用。
 *      Wire two distinct, dedicated pins together; leave input interrupts disabled.
 *      Call from normal task context.
 * @param output 输出引脚 / Output pin.
 * @param input 输入引脚 / Input pin.
 * @param settle_ms 电平稳定后的观察间隔 / Observation interval after reaching the level.
 * @param timeout_ms 每次等待电平的上限 / Deadline for each level wait.
 * @param iterations 高低电平循环次数 / Number of low/high cycles.
 */
inline void TestGPIO(GPIO& output, GPIO& input, uint32_t settle_ms = 1,
                     uint32_t timeout_ms = 1000, uint32_t iterations = 1000)
{
  TEST_ASSERT(&output != &input && iterations > 0);
  TEST_ASSERT(settle_ms > 0 && settle_ms < timeout_ms && timeout_ms < UINT32_MAX / 2U);
  TEST_ASSERT(input.SetConfig({GPIO::Direction::INPUT, GPIO::Pull::NONE}) ==
              ErrorCode::OK);
  TEST_ASSERT(output.SetConfig({GPIO::Direction::OUTPUT_PUSH_PULL, GPIO::Pull::NONE}) ==
              ErrorCode::OK);
  for (uint32_t i = 0; i < iterations; ++i)
  {
    output.Write(false);
    Detail::WaitForGPIOLevel(input, false, settle_ms, timeout_ms);
    output.Write(true);
    Detail::WaitForGPIOLevel(input, true, settle_ms, timeout_ms);
  }
  output.Write(false);
  Detail::WaitForGPIOLevel(input, false, settle_ms, timeout_ms);
}

/**
 * @brief 长期保留的 GPIO 中断测试对象 / Caller-retained GPIO interrupt test object.
 * @pre 引脚及本对象保留到复位，地址不变。输入中断资源须独占、无在途回调及旧挂起事件。
 *      Retain the pins and this object at stable addresses until reset. Reserve the
 *      input interrupt resource exclusively, with no active callback or old pending
 * event.
 */
class GPIOInterruptTest
{
 public:
  /**
   * @param expected_in_isr 预期回调是否在 ISR 中执行 / Whether callbacks should run in
   * ISR.
   */
  GPIOInterruptTest(GPIO& output, GPIO& input, bool expected_in_isr)
      : output_(output), input_(input), expected_in_isr_(expected_in_isr)
  {
  }
  GPIOInterruptTest(const GPIOInterruptTest&) = delete;
  GPIOInterruptTest& operator=(const GPIOInterruptTest&) = delete;

  /**
   * @brief 检查指定边沿及禁用后的回调次数 / Check selected edges and disabled callbacks.
   * @pre 从普通任务调用，每个对象只运行一次。结束后中断关闭，回调保持注册。
   *      Call once per object from normal task context. Leaves interrupts disabled
   *      and the callback registered.
   * @param edge 上升沿、下降沿或双沿 / Rising, falling or both edges.
   * @param settle_ms 无额外回调的观察间隔 / Observation interval for extra callbacks.
   * @param timeout_ms 每次电平或回调等待的上限 / Deadline for each level or callback
   * wait.
   * @param iterations 启用和禁用阶段各自的循环次数 / Cycles in each enabled/disabled
   * phase.
   */
  void Run(GPIO::Direction edge = GPIO::Direction::FALL_RISING_INTERRUPT,
           uint32_t settle_ms = 1, uint32_t timeout_ms = 1000, uint32_t iterations = 1000)
  {
    TEST_ASSERT(&output_ != &input_ && iterations > 0 && iterations <= UINT32_MAX / 2U);
    TEST_ASSERT(settle_ms > 0 && settle_ms < timeout_ms && timeout_ms < UINT32_MAX / 2U);
    TEST_ASSERT(edge == GPIO::Direction::RISING_INTERRUPT ||
                edge == GPIO::Direction::FALL_INTERRUPT ||
                edge == GPIO::Direction::FALL_RISING_INTERRUPT);
    TEST_ASSERT(input_.DisableInterrupt() == ErrorCode::OK);
    TEST_ASSERT(input_.SetConfig({GPIO::Direction::INPUT, GPIO::Pull::NONE}) ==
                ErrorCode::OK);
    TEST_ASSERT(output_.SetConfig({GPIO::Direction::OUTPUT_PUSH_PULL,
                                   GPIO::Pull::NONE}) == ErrorCode::OK);
    const bool initial = edge == GPIO::Direction::FALL_INTERRUPT;
    output_.Write(initial);
    Detail::WaitForGPIOLevel(input_, initial, settle_ms, timeout_ms);

    // 先稳定电平并绑定回调；部分平台配置边沿时就会启用中断。
    // Stabilize the line and bind first; some platforms enable IRQs in SetConfig.
    auto callback = GPIO::Callback::Create(
        [](bool in_isr, GPIOInterruptTest* self)
        {
          if (in_isr != self->expected_in_isr_)
          {
            self->bad_context_.store(1, std::memory_order_relaxed);
          }
          self->calls_.fetch_add(1, std::memory_order_release);
        },
        this);
    TEST_ASSERT(input_.RegisterCallback(callback) == ErrorCode::OK);
    TEST_ASSERT(input_.SetConfig({edge, GPIO::Pull::NONE}) == ErrorCode::OK);
    TEST_ASSERT(input_.EnableInterrupt() == ErrorCode::OK);
    uint32_t expected = 0;
    CheckCalls(expected, settle_ms, timeout_ms);
    for (uint32_t i = 0; i < iterations; ++i)
    {
      output_.Write(!initial);
      Detail::WaitForGPIOLevel(input_, !initial, settle_ms, timeout_ms);
      CheckCalls(++expected, settle_ms, timeout_ms);
      output_.Write(initial);
      Detail::WaitForGPIOLevel(input_, initial, settle_ms, timeout_ms);
      if (edge == GPIO::Direction::FALL_RISING_INTERRUPT)
      {
        ++expected;
      }
      CheckCalls(expected, settle_ms, timeout_ms);
    }

    TEST_ASSERT(input_.DisableInterrupt() == ErrorCode::OK);
    for (uint32_t i = 0; i < iterations; ++i)
    {
      output_.Write(true);
      Detail::WaitForGPIOLevel(input_, true, settle_ms, timeout_ms);
      CheckCalls(expected, settle_ms, timeout_ms);
      output_.Write(false);
      Detail::WaitForGPIOLevel(input_, false, settle_ms, timeout_ms);
      CheckCalls(expected, settle_ms, timeout_ms);
    }
    // 保持禁用；禁用期间的边沿可能留下挂起位，不能直接重新启用。
    // Leave disabled: masked edges may latch pending bits, so do not re-enable here.
  }

 private:
  void CheckCalls(uint32_t expected, uint32_t settle_ms, uint32_t timeout_ms)
  {
    const uint32_t start = Thread::GetTime();
    for (;;)
    {
      const uint32_t calls = calls_.load(std::memory_order_acquire);
      TEST_ASSERT(bad_context_.load(std::memory_order_relaxed) == 0);
      TEST_ASSERT(calls <= expected);
      if (calls == expected)
      {
        break;
      }
      TEST_ASSERT(Thread::GetTime() - start < timeout_ms);
      Thread::Sleep(1);
    }
    Thread::Sleep(settle_ms);
    TEST_ASSERT(calls_.load(std::memory_order_acquire) == expected);
    TEST_ASSERT(bad_context_.load(std::memory_order_relaxed) == 0);
  }

  GPIO& output_;
  GPIO& input_;
  const bool expected_in_isr_;
  std::atomic<uint32_t> calls_{0};
  std::atomic<uint32_t> bad_context_{0};
};
}  // namespace LibXR::Test
