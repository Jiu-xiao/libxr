#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Serialize UART hardware IRQ handling against configuration.
 * @brief 串行化 UART 硬件 IRQ 与配置事务。
 *
 * The IRQ ownership boundary must be entered before reading or clearing the hardware
 * completion/error status. Configuration can therefore stop/reset the old DMA and clear
 * its pending source while holding CONFIG_ACTIVE; an IRQ delayed before `TryEnterIrq()`
 * observes no old status after configuration releases the gate.
 *
 * IRQ ownership 必须在读取或清除硬件完成/错误状态前取得。配置持有 CONFIG_ACTIVE 时
 * 可以停止/复位旧 DMA 并清除 pending source；在 `TryEnterIrq()` 前被延迟的 IRQ 会在
 * 配置释放 gate 后重新读取硬件，因此不会发布旧状态。
 *
 * Multiple IRQ sources may attempt entry concurrently. CONFIG callers may also run on
 * other cores. An IRQ that cannot enter must return without touching hardware state;
 * its uncleared level/pending source is responsible for retriggering it later.
 * IRQ sources that can preempt each other on one core must use the same preemption
 * priority unless the platform independently guarantees that a losing source cannot
 * repeatedly preempt the current owner before it releases the gate.
 * 多个 IRQ 源可以并发尝试进入，CONFIG 也可在其他核执行。未取得 gate 的 IRQ 必须立即
 * 返回且不得触碰硬件状态；未清除的 level/pending source 负责随后重新触发该 IRQ。
 * 同一核心上可能互相抢占的 IRQ 必须使用相同抢占优先级，除非平台另有保证：失败入口不会在
 * 当前 owner 释放 gate 前反复抢占它。
 */
class UartIrqConfigGate
{
 public:
  void RequestConfig() { state_.fetch_or(CONFIG_PENDING, std::memory_order_release); }

  [[nodiscard]] bool TryEnterIrq()
  {
    uint32_t expected = 0U;
    return state_.compare_exchange_strong(expected, IRQ_ACTIVE, std::memory_order_acquire,
                                          std::memory_order_relaxed);
  }

  /**
   * @return true when CONFIG was requested while this IRQ owned the gate.
   */
  [[nodiscard]] bool LeaveIrq()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & IRQ_ACTIVE) != 0U);
      const uint32_t desired = observed & ~IRQ_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return (observed & CONFIG_PENDING) != 0U;
      }
    }
  }

  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t expected = CONFIG_PENDING;
    return state_.compare_exchange_strong(
        expected, CONFIG_ACTIVE, std::memory_order_acquire, std::memory_order_relaxed);
  }

  /**
   * @brief Enter one IRQ that belongs to an active asynchronous config transaction.
   * @return true when CONFIG remains owner and this caller acquired IRQ serialization.
   */
  [[nodiscard]] bool TryEnterConfigIrq()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (((observed & CONFIG_ACTIVE) != 0U) && ((observed & IRQ_ACTIVE) == 0U))
    {
      const uint32_t desired = observed | IRQ_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool IrqActive() const
  {
    return (state_.load(std::memory_order_acquire) & IRQ_ACTIVE) != 0U;
  }

  [[nodiscard]] bool ConfigRequested() const
  {
    return (state_.load(std::memory_order_acquire) & (CONFIG_PENDING | CONFIG_ACTIVE)) !=
           0U;
  }

  void ConsumePendingConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      const uint32_t desired = observed & ~CONFIG_PENDING;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  void LeaveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      const uint32_t desired = observed & ~CONFIG_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  UartIrqConfigGate() = default;
  UartIrqConfigGate(const UartIrqConfigGate&) = delete;
  UartIrqConfigGate& operator=(const UartIrqConfigGate&) = delete;

 private:
  static constexpr uint32_t IRQ_ACTIVE = 1U << 0U;
  static constexpr uint32_t CONFIG_PENDING = 1U << 1U;
  static constexpr uint32_t CONFIG_ACTIVE = 1U << 2U;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
