#pragma once

#include <cstdint>

namespace LibXR
{

namespace Detail
{

/**
 * @brief 计算 32 位环形 tick 上的前向距离 / Compute a forward distance on a
 * 32-bit wrapping tick ring
 * @return 目标在未来半环时返回距离；目标已到或在过去半环时返回 0。 / Distance when
 * the target is in the future half-ring, otherwise zero for a reached or past target.
 */
[[nodiscard]] constexpr uint32_t SchedulerTicksUntil(uint32_t current,
                                                     uint32_t target) noexcept
{
  const uint32_t distance = target - current;
  constexpr uint32_t HALF_RANGE = UINT32_C(1) << 31U;
  return distance < HALF_RANGE ? distance : 0U;
}

}  // namespace Detail

class Thread;

/**
 * @brief 线程调度器时间戳 / Thread scheduler timestamp
 *
 * 该类型只表示 `Thread::GetTime()` 与 `Thread::SleepUntil()` 使用的调度时钟域。
 * 内部数值采用当前后端的原生调度 tick，不保证单位是毫秒。它不能从 Timebase
 * 时间戳或整数构造，也不提供到这些类型的转换运算符。
 * This type belongs exclusively to the scheduler clock used by `Thread::GetTime()`
 * and `Thread::SleepUntil()`. Its value uses the backend's native scheduler tick and is
 * not guaranteed to be milliseconds. It cannot be constructed from or converted to
 * Timebase timestamps or integers through conversion operators.
 */
class ThreadTimestamp
{
 public:
  constexpr ThreadTimestamp() = default;

  /**
   * @brief 读取底层调度 tick / Read the raw scheduler tick value
   *
   * 该接口用于平台适配和诊断，不表示能够转换到其他时钟域。实际计数器可能窄于
   * 32 位，因此可移植代码不能直接对返回值做时间差或顺序判断。
   * This is for platform adapters and diagnostics; it does not define a conversion to
   * another clock domain. The native counter may be narrower than 32 bits, so portable
   * code must not use the returned integer for elapsed-time or ordering calculations.
   */
  [[nodiscard]] constexpr uint32_t RawTicks() const noexcept { return ticks_; }

  friend constexpr bool operator==(ThreadTimestamp, ThreadTimestamp) = default;

 private:
  explicit constexpr ThreadTimestamp(uint32_t ticks) noexcept : ticks_(ticks) {}

  uint32_t ticks_ = 0U;

  friend class Thread;
};

static_assert(sizeof(ThreadTimestamp) == sizeof(uint32_t));

}  // namespace LibXR
