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
 * events. A caller that acquires the owner drains events in its current execution
 * context until a no-new-event release CAS succeeds. A caller that observes an owner
 * only publishes its events and returns.
 *
 * Events do not preserve counts or payloads. Every invocation for one service instance
 * must use the same logical handler. The handler must not throw or block; when an ISR can
 * acquire the owner, all work reachable from the handler must be ISR-safe.
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
   * @brief Publish events and claim/release the owner inside a backend admission guard.
   *
   * The guard serializes a multi-step IRQ-domain mask with the owner claim, and the
   * no-new-event release CAS with the corresponding restore. It is held only around
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
   * @brief Claim before reading an IRQ source, using the backend admission guard.
   *
   * The protected source is read only after the owner CAS succeeds and after the short
   * admission guard has been released. The IRQ domain remains masked until the guarded
   * release CAS restores it.
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
                                         std::memory_order_acquire))
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
                                         std::memory_order_acquire))
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
