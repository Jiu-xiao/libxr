#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief 在一个不可重入 owner 下合并电平型工作 / Coalesce level work under one owner.
 *
 * Owner 和事件共享一个原子字；Invoke 的 CAS 胜者执行自己的 handler，直到事件清空。
 * 失败方只发布事件，不保存其 handler。所有 handler 必须能处理全部事件种类；事件位
 * 不保存次数。对象须存活至所有调用结束；handler 不得抛异常或等待当前 owner 的进度。
 * The owner and events share one atomic word. The winning Invoke runs its own handler
 * until events drain; losing callables are never retained. Every handler must handle all
 * event kinds and drain level work because bits do not count occurrences. The service
 * must outlive all calls. Handlers must not throw or wait on progress held by this owner.
 *
 * 原子实现和内存必须适合调用上下文；本类不提供 MMIO、DMA 或生命周期同步。
 * Atomic storage/runtime must support the calling context. This class does not serialize
 * raw MMIO, synchronize DMA caches, or provide backend teardown coordination.
 */
class SerializedService
{
 public:
  SerializedService() = default;

  /**
   * @brief 发布事件并尝试取得 owner / Publish events and attempt ownership.
   * @param events 非零低 31 位事件 / Nonzero event bits below bit 31.
   * @param in_isr 当前调用上下文，仅传给胜者 / Context passed to the winning owner.
   * @param handler 签名 void(uint32_t, bool) / Callable as void(uint32_t, bool).
   * @return 当前调用执行了 handler 则为 true / True if this invocation ran its handler.
   */
  template <typename Handler>
  bool Invoke(uint32_t events, bool in_isr, Handler&& handler) noexcept
  {
    ASSERT(IsEventMask(events));
    if (!IsEventMask(events))
    {
      return false;
    }

    uint32_t observed = state_.fetch_or(events, std::memory_order_release) | events;
    while ((observed & OWNER_BIT) == 0U)
    {
      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        Handler& handler_ref = handler;
        Drain(handler_ref, in_isr, observed & EVENT_MASK);
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 为已有执行保证的 owner 发布事件 / Publish for a guaranteed carrier.
   * @param events 低 31 位事件；零值不操作 / Low-31-bit events; zero is a no-op.
   * @pre 已有 owner 或保证后续 Invoke；本方法本身不调度执行 /
   *      An owner or later Invoke is guaranteed; this method does not schedule work.
   */
  void Publish(uint32_t events) noexcept
  {
    if (events == 0U)
    {
      return;
    }
    ASSERT(IsEventMask(events));
    if (IsEventMask(events))
    {
      state_.fetch_or(events, std::memory_order_release);
    }
  }

  SerializedService(const SerializedService&) = delete;
  SerializedService& operator=(const SerializedService&) = delete;

 private:
  static constexpr uint32_t OWNER_BIT = 1U << 31U;
  static constexpr uint32_t EVENT_MASK = ~OWNER_BIT;

  static constexpr bool IsEventMask(uint32_t events)
  {
    return events != 0U && (events & OWNER_BIT) == 0U;
  }

  template <typename Handler>
  void Drain(Handler& handler, bool in_isr, uint32_t snapshot) noexcept
  {
    while (true)
    {
      handler(snapshot, in_isr);

      uint32_t expected = OWNER_BIT;
      if (state_.compare_exchange_strong(expected, 0U, std::memory_order_release,
                                         std::memory_order_relaxed))
      {
        return;
      }

      snapshot = state_.exchange(OWNER_BIT, std::memory_order_acq_rel) & EVENT_MASK;
    }
  }

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
