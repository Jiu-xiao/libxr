/**
 * @file test_dac.hpp
 * @brief 用 ADC 回读 DAC 输出的测试 / DAC output test using ADC readback.
 *
 * 在调用方指定的电压范围内反复升降，每个电压点通过 TestADC 检查连续读数。
 * Repeatedly step up and down within the caller's voltage range and use TestADC
 * to check consecutive readings at every level. Leave the output at the lower level.
 */
#pragma once

#include "adc/test_adc.hpp"
#include "dac.hpp"

namespace LibXR::Test
{
/**
 * @brief 反复设置 DAC 电压并检查 ADC 回读 / Repeatedly set DAC voltage and check ADC
 * reads.
 * @pre DAC 输出接 ADC 输入，两个设备和系统已初始化，从普通任务调用。
 *      Wire DAC output to ADC input, initialize both devices and the system,
 *      and call from normal task context.
 * @pre 电压范围须同时满足 DAC 输出与 ADC 输入要求，稳定时间须包含采样缓冲区更新。
 *      Choose a range supported by both devices and allow settling time for
 *      the circuit and any ADC sample buffer update.
 * @param dac 被测 DAC 输出 / DAC output under test.
 * @param adc 用于回读的 ADC 通道 / ADC channel used for readback.
 * @param min_voltage 测试最低电压，V / Lower test voltage in V.
 * @param max_voltage 测试最高电压，V / Upper test voltage in V.
 * @param tolerance_voltage 每次读数的绝对误差上限，V，须小于相邻电压差的一半 /
 *        Absolute tolerance per reading in V, less than half the adjacent level spacing.
 * @param settle_ms 每次写入后的固定等待时间 / Fixed delay after each write in
 * milliseconds.
 * @param iterations 完整升降轮数 / Number of complete up/down rounds.
 */
inline void TestDAC(DAC& dac, ADC& adc, float min_voltage, float max_voltage,
                    float tolerance_voltage, uint32_t settle_ms = 5,
                    uint32_t iterations = 100)
{
  TEST_ASSERT(std::isfinite(min_voltage) && std::isfinite(max_voltage));
  const float middle =
      static_cast<float>((static_cast<double>(min_voltage) + max_voltage) / 2.0);
  TEST_ASSERT(min_voltage < middle && middle < max_voltage);
  TEST_ASSERT(std::isfinite(tolerance_voltage) && tolerance_voltage >= 0.0f);
  TEST_ASSERT(2.0 * tolerance_voltage < static_cast<double>(middle) - min_voltage);
  TEST_ASSERT(2.0 * tolerance_voltage < static_cast<double>(max_voltage) - middle);
  TEST_ASSERT(iterations > 0 && settle_ms < UINT32_MAX / 2U);

  const float voltages[] = {min_voltage, middle, max_voltage, middle, min_voltage};
  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (float voltage : voltages)
    {
      TEST_ASSERT(dac.Write(voltage) == ErrorCode::OK);
      Thread::Sleep(settle_ms);
      // 每个电压点检查 16 次，间隔 1 ms；超差立即失败，不延长等待重试。
      // Check 16 readings, 1 ms apart; fail on an outlier without extending the wait.
      TestADC(adc, voltage, tolerance_voltage, 1, 16);
    }
  }
}
}  // namespace LibXR::Test
