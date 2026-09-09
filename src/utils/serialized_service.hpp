#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief 事件合并与回调串行化服务 / Event-coalescing serialized-callback service.
 *
 * 该服务用一个原子字保存执行权和低 31 位事件。`Invoke()` 发布事件并尝试取得执行权；
 * 只有取得执行权的调用执行 handler，执行期间到达的事件由同一调用继续处理。未取得执行
 * 权的调用只发布事件，其 handler 不会保存或稍后调用。事件位表示状态需要重新检查，不
 * 表示发生次数。
 *
 * This service stores execution ownership and low-31-bit events in one atomic word.
 * `Invoke()` publishes events and attempts to claim execution. Only the caller that
 * claims execution runs the handler; events arriving during execution are processed
 * before ownership is released. A losing caller's handler is never retained or called
 * later. Event bits request a state re-check and do not count occurrences.
 *
 * `Publish()` 只记录事件，不负责唤醒或调度。service 以及 handler 访问的数据必须在所有活动
 * 调用结束前保持有效；handler 不得抛出异常，也不得等待当前 service 所保护的进度。原子
 * 操作的上下文限制、MMIO、DMA 同步和后端生命周期由调用方负责。
 *
 * `Publish()` records an event but does not wake or schedule execution. The service and
 * all data accessed by its handler must outlive every active call. Handlers must not
 * throw or wait for progress protected by this service. Context restrictions for atomics,
 * MMIO, DMA synchronization, and backend lifetime remain the caller's responsibility.
 */
class SerializedService
{
 public:
  SerializedService() = default;

  /**
   * @brief 发布事件并尝试取得执行权 / Publish events and attempt to claim execution.
   * @param events 非零低 31 位事件 / Nonzero event bits below bit 31.
   * @param in_isr 当前调用上下文；仅传给取得执行权的调用 /
   *        Context of this call; used only if it claims execution.
   * @param handler 签名 void(uint32_t, bool) / Callable as void(uint32_t, bool).
   * @return 当前调用执行了 handler 则为 true；否则仅发布事件并返回 false /
   *         True if this call ran the handler; false if it only published events.
   */
  template <typename Handler>
  bool Invoke(uint32_t events, bool in_isr, Handler&& handler) noexcept
  {
    ASSERT(IsEventMask(events));

    uint32_t observed = state_.fetch_or(events, std::memory_order_release) | events;
    while ((observed & OWNER_BIT) == 0U)
    {
      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        Handler& handler_ref = handler;
        return Drain(handler_ref, in_isr, observed & EVENT_MASK);
      }
    }
    return false;
  }

  /**
   * @brief 为已有或确定会出现的执行者发布事件 / Publish for an active or guaranteed
   *        executor.
   * @param events 低 31 位事件；零值不操作 / Low-31-bit events; zero is a no-op.
   * @pre 已有执行者或保证后续 `Invoke`；本方法本身不调度执行 /
   *      An executor or a later `Invoke` is guaranteed; this method does not schedule
   *      execution.
   */
  void Publish(uint32_t events) noexcept
  {
    if (events == 0U)
    {
      return;
    }
    ASSERT(IsEventMask(events));
    state_.fetch_or(events, std::memory_order_release);
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
  bool Drain(Handler& handler, bool in_isr, uint32_t snapshot) noexcept
  {
    bool invoked = false;
    while (true)
    {
      if (snapshot != 0U)
      {
        invoked = true;
        handler(snapshot, in_isr);
      }

      uint32_t expected = OWNER_BIT;
      if (state_.compare_exchange_strong(expected, 0U, std::memory_order_release,
                                         std::memory_order_relaxed))
      {
        return invoked;
      }

      snapshot = state_.exchange(OWNER_BIT, std::memory_order_acq_rel) & EVENT_MASK;
    }
  }

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
