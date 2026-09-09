/**
 * @file test_pwm.hpp
 * @brief PWM 接线回读测试 / Wired PWM readback test.
 *
 * 用 GPIO 轮询测量两种频率下的周期和高电平时间，检查 0%/100% 占空比及启停。
 * Poll a GPIO to measure period and high time at two frequencies; check 0%/100%
 * duty cycles and disabling/re-enabling. Intended for low-frequency signals.
 */
#pragma once

#include "gpio.hpp"
#include "pwm.hpp"
#include "test_assert.hpp"
#include "timebase.hpp"

namespace LibXR::Test
{
/**
 * @brief 反复检查 PWM 波形、占空比和启停 / Repeatedly check PWM timing, duty and enable.
 * @pre 输出接专用输入，PWM 高电平有效，输入支持下拉且中断已关闭。独占 PWM 定时器。
 *      Connect active-high PWM to a dedicated input supporting pull-down, with its
 *      interrupts disabled. Reserve the PWM timer exclusively.
 * @pre 微秒时间基已初始化。从普通任务调用；采样和抢占延迟须远小于最短脉宽。
 *      Initialize the microsecond timebase. Call from normal task context; sampling
 *      and preemption delays must be much shorter than the shortest pulse.
 * @param output 已初始化的 PWM / Initialized PWM output.
 * @param input 接线回读输入 / Wired readback input.
 * @param frequency_hz 测试此频率及其两倍 / Test this frequency and twice this value.
 * @param tolerance_us 周期及高电平时间的绝对误差上限 / Absolute period/high-time
 * tolerance.
 * @param iterations 完整测试轮数 / Number of complete rounds.
 */
inline void TestPWM(PWM& output, GPIO& input, uint32_t frequency_hz = 100,
                    uint32_t tolerance_us = 100, uint32_t iterations = 100)
{
  TEST_ASSERT(Timebase::IsReady() && iterations > 0);
  TEST_ASSERT(frequency_hz > 0 && frequency_hz <= 500000U);
  TEST_ASSERT(tolerance_us > 0 && tolerance_us <= (1000000U / (frequency_hz * 2U)) / 20U);
  TEST_ASSERT(input.SetConfig({GPIO::Direction::INPUT, GPIO::Pull::DOWN}) ==
              ErrorCode::OK);

  auto elapsed = [](MicrosecondTimestamp start)
  { return (Timebase::GetMicroseconds() - start).ToMicrosecond(); };
  auto settle = [&](uint32_t period_us)
  {
    const auto start = Timebase::GetMicroseconds();
    // 预装载寄存器可能在下个周期更新，测量前固定等待两个周期。
    // Preloaded values may update next cycle; always settle for two cycles.
    while (elapsed(start) < 2U * period_us)
    {
    }
  };
  auto wait_level = [&](bool level, uint32_t period_us)
  {
    const auto start = Timebase::GetMicroseconds();
    while (input.Read() != level)
    {
      TEST_ASSERT(elapsed(start) < 2U * period_us);
    }
    return Timebase::GetMicroseconds();
  };
  auto check_stable = [&](bool level, uint32_t period_us)
  {
    const auto start = Timebase::GetMicroseconds();
    do
    {
      TEST_ASSERT(input.Read() == level);
    } while (elapsed(start) < 2U * period_us);
  };
  auto check_time = [&](uint64_t actual, uint32_t expected)
  {
    const uint64_t error = actual > expected ? actual - expected : expected - actual;
    TEST_ASSERT(error <= tolerance_us);
  };
  auto check_waveform = [&](uint32_t period_us, uint32_t duty_percent)
  {
    // 先同步到低电平，再取相邻的上升、下降、上升沿；不重试失败的周期。
    // Synchronize low, then capture consecutive rising/falling/rising edges; no retries.
    wait_level(false, period_us);
    const auto rise = wait_level(true, period_us);
    const auto fall = wait_level(false, period_us);
    const auto next_rise = wait_level(true, period_us);
    check_time((next_rise - rise).ToMicrosecond(), period_us);
    check_time((fall - rise).ToMicrosecond(), period_us * duty_percent / 100U);
  };

  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (uint32_t multiplier = 1; multiplier <= 2; ++multiplier)
    {
      const uint32_t frequency = frequency_hz * multiplier;
      const uint32_t period_us = 1000000U / frequency;
      TEST_ASSERT(output.Disable() == ErrorCode::OK);
      TEST_ASSERT(output.SetConfig({frequency}) == ErrorCode::OK);
      TEST_ASSERT(output.SetDutyCycle(0.25f) == ErrorCode::OK);
      TEST_ASSERT(output.Enable() == ErrorCode::OK);
      for (uint32_t duty = 25; duty <= 75; duty += 25)
      {
        TEST_ASSERT(output.SetDutyCycle(static_cast<float>(duty) / 100.0f) ==
                    ErrorCode::OK);
        settle(period_us);
        check_waveform(period_us, duty);
      }

      TEST_ASSERT(output.SetDutyCycle(0.0f) == ErrorCode::OK);
      settle(period_us);
      check_stable(false, period_us);
      TEST_ASSERT(output.SetDutyCycle(1.0f) == ErrorCode::OK);
      settle(period_us);
      check_stable(true, period_us);

      TEST_ASSERT(output.SetDutyCycle(0.5f) == ErrorCode::OK);
      settle(period_us);
      TEST_ASSERT(output.Disable() == ErrorCode::OK);
      settle(period_us);
      // 禁用可能保持某个电平，也可能高阻；只要求不再出现周期跳变。
      // Disable may hold a level or become high-impedance; require no periodic edges.
      check_stable(input.Read(), period_us);
      TEST_ASSERT(output.Enable() == ErrorCode::OK);
      settle(period_us);
      check_waveform(period_us, 50);
    }
  }
  TEST_ASSERT(output.SetDutyCycle(0.0f) == ErrorCode::OK);
  TEST_ASSERT(output.Disable() == ErrorCode::OK);
}
}  // namespace LibXR::Test
