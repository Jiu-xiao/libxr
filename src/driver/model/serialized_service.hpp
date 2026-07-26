#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Coalesce level-triggered events and run one non-reentrant service owner.
 *
 * The high bit of one atomic word is the owner bit. The low 31 bits are coalescible
 * events. One atomic OR publishes events and claims the owner without disturbing
 * concurrent publications; atomic AND takes event snapshots and releases the owner
 * while preserving later publications. A caller that observes an owner only publishes
 * its events and returns.
 *
 * Events do not preserve counts or payloads. Every invocation for one service instance
 * must use the same logical handler. The handler must not throw or block; when an ISR can
 * acquire the owner, all work reachable from the handler must be ISR-safe. One instance
 * must consistently use either guarded or unguarded owner-entry methods; mixing them
 * bypasses the guarded IRQ-domain admission contract.
 */
class SerializedService
{
 public:
  SerializedService() = default;

  /**
   * @brief Publish events and try to acquire the service owner.
   * @tparam Handler Callable with signature `void(uint32_t)`.
   * @param events Nonzero low-31-bit event mask.
   * @param handler Handler for each consumed event snapshot.
   * @return true when this invocation acquired and released the owner; false when the
   * event was handed to another owner or had already been consumed.
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    ASSERT(IsEventMask(events));
    if (!IsEventMask(events))
    {
      return false;
    }

    const uint32_t claimed =
        state_.fetch_or(events | OWNER_BIT, std::memory_order_acq_rel);
    if ((claimed & OWNER_BIT) != 0U)
    {
      return false;
    }

    Handler& handler_ref = handler;
    Drain(TakePendingEvents(), handler_ref);
    return true;
  }

  /**
   * @brief Publish events and claim/release the owner inside a backend admission guard.
   *
   * The guard serializes a multi-step IRQ-domain mask with the owner claim, and the
   * no-new-event owner release with the corresponding restore. It is held only around
   * those fixed-cost boundaries, never while the service handler runs.
   *
   * @tparam Guard Backend guard providing `LockAndMaskIrqDomain()`,
   * `UnlockIrqDomain()`, `LockIrqDomain()`, and `RestoreAndUnlockIrqDomain()`.
   * @tparam Handler Callable with signature `void(uint32_t)`.
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
    const uint32_t claimed =
        state_.fetch_or(events | OWNER_BIT, std::memory_order_acq_rel);
    if ((claimed & OWNER_BIT) != 0U)
    {
      guard.UnlockIrqDomain();
      return false;
    }

    guard.UnlockIrqDomain();
    Handler& handler_ref = handler;
    DrainGuarded(TakePendingEvents(), handler_ref, guard);
    return true;
  }

  /**
   * @brief Claim an owner before obtaining events from an owned source.
   *
   * This entry is for an IRQ adapter that must acquire the same owner before reading or
   * acknowledging protected hardware status. `source` is called only after a successful
   * owner claim and returns a low-31-bit event mask. Events queued before the claim are
   * consumed in the same first snapshot.
   *
   * @tparam Source Callable with signature `uint32_t()`.
   * @tparam Handler Callable with signature `void(uint32_t)`.
   * @return true when this invocation acquired and released the owner; false when an
   * existing owner must eventually restore/retrigger the source.
   */
  template <typename Source, typename Handler>
  bool ClaimAndInvoke(Source&& source, Handler&& handler) noexcept
  {
    const uint32_t claimed = state_.fetch_or(OWNER_BIT, std::memory_order_acquire);
    if ((claimed & OWNER_BIT) != 0U)
    {
      return false;
    }

    const uint32_t pending_events = TakePendingEvents();
    Source& source_ref = source;
    const uint32_t source_events = source_ref();
    ASSERT((source_events & OWNER_BIT) == 0U);

    Handler& handler_ref = handler;
    Drain(pending_events | source_events, handler_ref);
    return true;
  }

  /**
   * @brief Claim before reading an IRQ source, using the backend admission guard.
   *
   * The protected source is read only after the owner claim succeeds and after the short
   * admission guard has been released. The IRQ domain remains masked until the guarded
   * owner release restores it.
   */
  template <typename Guard, typename Source, typename Handler>
  bool ClaimAndInvokeGuarded(Guard& guard, Source&& source, Handler&& handler) noexcept
  {
    guard.LockAndMaskIrqDomain();
    const uint32_t claimed = state_.fetch_or(OWNER_BIT, std::memory_order_acquire);
    if ((claimed & OWNER_BIT) != 0U)
    {
      guard.UnlockIrqDomain();
      return false;
    }

    guard.UnlockIrqDomain();

    const uint32_t pending_events = TakePendingEvents();
    Source& source_ref = source;
    const uint32_t source_events = source_ref();
    ASSERT((source_events & OWNER_BIT) == 0U);

    Handler& handler_ref = handler;
    DrainGuarded(pending_events | source_events, handler_ref, guard);
    return true;
  }

  /**
   * @brief Publish events without trying to acquire the service owner.
   *
   * An active owner consumes the events before release. If no owner exists, the caller
   * must guarantee a later `Invoke()` or `ClaimAndInvoke()` carrier. Ordinary publishers
   * should use `Invoke()` so owner-free work starts immediately.
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

  uint32_t TakePendingEvents() noexcept
  {
    const uint32_t observed = state_.fetch_and(OWNER_BIT, std::memory_order_acq_rel);
    ASSERT((observed & OWNER_BIT) != 0U);
    return observed & EVENT_MASK;
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

      snapshot = TakePendingEvents();
      if (snapshot != 0U)
      {
        continue;
      }

      const uint32_t released = state_.fetch_and(EVENT_MASK, std::memory_order_release);
      ASSERT((released & OWNER_BIT) != 0U);
      if ((released & EVENT_MASK) == 0U)
      {
        return;
      }

      const uint32_t claimed = state_.fetch_or(OWNER_BIT, std::memory_order_acquire);
      if ((claimed & OWNER_BIT) != 0U)
      {
        return;
      }
      snapshot = TakePendingEvents();
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

      snapshot = TakePendingEvents();
      if (snapshot != 0U)
      {
        continue;
      }

      guard.LockIrqDomain();
      const uint32_t released = state_.fetch_and(EVENT_MASK, std::memory_order_release);
      ASSERT((released & OWNER_BIT) != 0U);
      if ((released & EVENT_MASK) == 0U)
      {
        guard.RestoreAndUnlockIrqDomain();
        return;
      }

      const uint32_t claimed = state_.fetch_or(OWNER_BIT, std::memory_order_acquire);
      ASSERT((claimed & OWNER_BIT) == 0U);
      guard.UnlockIrqDomain();
      snapshot = TakePendingEvents();
    }
  }

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
