#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief 合并电平型事件并由唯一、不可重入的 owner 串行处理 / Coalesce
 * level-triggered events under one non-reentrant owner
 *
 * 原子状态字最高位为 owner 位，低 31 位为可合并事件。抢到 owner 的调用者在当前执行
 * 上下文持续处理事件，直到“没有新事件”的释放 CAS 成功；观察到已有 owner 的调用者只
 * 发布事件并返回。 / The high bit is the owner bit and the low 31 bits are coalescible
 * events. The owner drains events in its current context until a no-new-event release
 * CAS succeeds; other callers only publish and return.
 *
 * 事件不保留次数和 payload。同一实例的所有调用必须使用同一个逻辑 handler。handler
 * 不得抛异常或阻塞；若 ISR 可以取得 owner，则其可达工作都必须 ISR-safe。 / Events do
 * not preserve counts or payloads. All invocations for one instance must use the same
 * logical handler. The handler must not throw or block, and must be ISR-safe whenever an
 * ISR can acquire the owner.
 */
class SerializedService
{
 public:
  /** @brief 构造一个空闲的串行服务 / Construct an idle serialized service. */
  SerializedService() = default;

  /**
   * @brief 发布事件并尝试取得 service owner / Publish events and try to acquire the
   * service owner
   * @tparam Handler 签名为 `void(uint32_t)` 的可调用对象 / Callable with signature
   * `void(uint32_t)`
   * @param events 非零的低 31 位事件掩码 / Nonzero low-31-bit event mask
   * @param handler 逐个处理已消费事件快照的 handler / Handler for each consumed event
   * snapshot
   * @return 本调用取得并释放 owner 时为 true；事件交给其他 owner 或已被消费时为 false /
   * True when this call acquired and released the owner; false when another owner
   * accepted or already consumed the event
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    ASSERT(IsEventMask(events));
    if (!IsEventMask(events))
    {
      return false;
    }

    uint32_t observed = state_.fetch_or(events, std::memory_order_release) | events;
    while ((observed & OWNER_BIT) == 0U)
    {
      if ((observed & EVENT_MASK) == 0U)
      {
        return false;
      }

      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        Handler& handler_ref = handler;
        Drain(observed & EVENT_MASK, handler_ref);
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 在后端 admission guard 内发布事件并取得或释放 owner / Publish events and
   * claim or release the owner inside a backend admission guard
   *
   * guard 将多步骤 IRQ 域屏蔽与 owner claim 串行化，并将无新事件的释放 CAS 与对应恢复
   * 串行化。guard 只覆盖这些固定开销边界，不覆盖 handler 执行。 / The guard serializes
   * the IRQ-domain mask with owner claim and the release CAS with restore. It covers
   * only those fixed-cost boundaries, never handler execution.
   *
   * @tparam Guard 提供 `LockAndMaskIrqDomain()`、`UnlockIrqDomain()`、
   * `LockIrqDomain()` 和 `RestoreAndUnlockIrqDomain()` 的后端 guard / Backend guard
   * providing the IRQ-domain lock, mask, restore, and unlock operations
   * @tparam Handler 签名为 `void(uint32_t)` 的可调用对象 / Callable with signature
   * `void(uint32_t)`
   * @param events 非零的低 31 位事件掩码 / Nonzero low-31-bit event mask
   * @param guard 后端 admission guard / Backend admission guard
   * @param handler 逐个处理事件快照的 handler / Handler for each event snapshot
   * @return 本调用取得并释放 owner 时为 true，否则为 false / True when this call
   * acquired and released the owner; false otherwise
   */
  template <typename Guard, typename Handler>
  bool InvokeGuarded(uint32_t events, Guard& guard, Handler&& handler) noexcept
  {
    ASSERT(IsEventMask(events));
    if (!IsEventMask(events))
    {
      return false;
    }

    guard.LockAndMaskIrqDomain();
    uint32_t observed = state_.fetch_or(events, std::memory_order_release) | events;
    while ((observed & OWNER_BIT) == 0U)
    {
      if ((observed & EVENT_MASK) == 0U)
      {
        guard.UnlockIrqDomain();
        return false;
      }

      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        guard.UnlockIrqDomain();
        Handler& handler_ref = handler;
        DrainGuarded(observed & EVENT_MASK, handler_ref, guard);
        return true;
      }
    }
    guard.UnlockIrqDomain();
    return false;
  }

  /**
   * @brief 在读取受控事件源之前取得 owner / Claim the owner before reading an owned
   * event source
   *
   * 该入口用于必须先取得同一 owner、再读或确认受保护硬件状态的 IRQ adapter。`source`
   * 只在 claim 成功后调用并返回低 31 位事件掩码；claim 前已发布的事件与其组成首个快照。
   * / This entry serves an IRQ adapter that must own the service before reading or
   * acknowledging protected hardware status. `source` runs only after a successful
   * claim, and its events join already queued events in the first snapshot.
   *
   * @tparam Source 签名为 `uint32_t()` 的受控事件源 / Owned source callable with
   * signature `uint32_t()`
   * @tparam Handler 签名为 `void(uint32_t)` 的可调用对象 / Callable with signature
   * `void(uint32_t)`
   * @param source claim 成功后读取并确认的事件源 / Source read and acknowledged after
   * a successful claim
   * @param handler 逐个处理事件快照的 handler / Handler for each event snapshot
   * @return 本调用取得并释放 owner 时为 true；已有 owner 必须稍后恢复或重触发事件源时
   * 为 false / True when this call acquired and released the owner; false when an
   * existing owner must later restore or retrigger the source
   */
  template <typename Source, typename Handler>
  bool ClaimAndInvoke(Source&& source, Handler&& handler) noexcept
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while ((observed & OWNER_BIT) == 0U)
    {
      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        Source& source_ref = source;
        const uint32_t source_events = source_ref();
        ASSERT((source_events & OWNER_BIT) == 0U);

        Handler& handler_ref = handler;
        Drain((observed | source_events) & EVENT_MASK, handler_ref);
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 使用后端 admission guard，在读取 IRQ 源前取得 owner / Claim before reading
   * an IRQ source under the backend admission guard
   *
   * 受保护事件源只在 owner CAS 成功且短 guard 已释放后读取；IRQ 域保持屏蔽，直到 guarded
   * release CAS 恢复它。 / The protected source is read only after owner CAS succeeds
   * and the short guard is released. The IRQ domain remains masked until the guarded
   * release CAS restores it.
   *
   * @tparam Guard 后端 admission guard 类型 / Backend admission guard type
   * @tparam Source 签名为 `uint32_t()` 的受控 IRQ 源 / Owned IRQ source callable with
   * signature `uint32_t()`
   * @tparam Handler 签名为 `void(uint32_t)` 的可调用对象 / Callable with signature
   * `void(uint32_t)`
   * @param guard 后端 admission guard / Backend admission guard
   * @param source claim 成功后读取并确认的 IRQ 源 / IRQ source read and acknowledged
   * after a successful claim
   * @param handler 逐个处理事件快照的 handler / Handler for each event snapshot
   * @return 本调用取得并释放 owner 时为 true，否则为 false / True when this call
   * acquired and released the owner; false otherwise
   */
  template <typename Guard, typename Source, typename Handler>
  bool ClaimAndInvokeGuarded(Guard& guard, Source&& source, Handler&& handler) noexcept
  {
    guard.LockAndMaskIrqDomain();
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while ((observed & OWNER_BIT) == 0U)
    {
      if (state_.compare_exchange_weak(observed, OWNER_BIT, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        guard.UnlockIrqDomain();

        Source& source_ref = source;
        const uint32_t source_events = source_ref();
        ASSERT((source_events & OWNER_BIT) == 0U);

        Handler& handler_ref = handler;
        DrainGuarded((observed | source_events) & EVENT_MASK, handler_ref, guard);
        return true;
      }
    }
    guard.UnlockIrqDomain();
    return false;
  }

  /**
   * @brief 只发布事件，不尝试取得 service owner / Publish events without trying to
   * acquire the service owner
   *
   * active owner 会在释放前消费事件。若当前没有 owner，调用者必须保证稍后存在
   * `Invoke()` 或 `ClaimAndInvoke()` carrier；普通发布者应使用 `Invoke()` 立即启动无人
   * 持有的工作。 / An active owner consumes the events before release. With no owner,
   * the caller must guarantee a later `Invoke()` or `ClaimAndInvoke()` carrier. Ordinary
   * publishers should use `Invoke()` so owner-free work starts immediately.
   *
   * @param events 低 31 位事件掩码；零值不执行操作 / Low-31-bit event mask; zero is a
   * no-op
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
    return (events != 0U) && ((events & OWNER_BIT) == 0U);
  }

  template <typename Handler>
  void Drain(uint32_t snapshot, Handler& handler) noexcept
  {
    while (true)
    {
      if (snapshot != 0U)
      {
        handler(snapshot);
      }

      uint32_t expected = OWNER_BIT;
      if (state_.compare_exchange_strong(expected, 0U, std::memory_order_release,
                                         std::memory_order_relaxed))
      {
        return;
      }

      snapshot = state_.exchange(OWNER_BIT, std::memory_order_acq_rel) & EVENT_MASK;
    }
  }

  template <typename Handler, typename Guard>
  void DrainGuarded(uint32_t snapshot, Handler& handler, Guard& guard) noexcept
  {
    while (true)
    {
      if (snapshot != 0U)
      {
        handler(snapshot);
      }

      guard.LockIrqDomain();
      uint32_t expected = OWNER_BIT;
      if (state_.compare_exchange_strong(expected, 0U, std::memory_order_release,
                                         std::memory_order_relaxed))
      {
        guard.RestoreAndUnlockIrqDomain();
        return;
      }
      guard.UnlockIrqDomain();

      snapshot = state_.exchange(OWNER_BIT, std::memory_order_acq_rel) & EVENT_MASK;
    }
  }

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
