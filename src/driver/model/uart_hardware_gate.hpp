#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Serialize UART hardware access shared by IRQ, TX start, and configuration.
 *
 * All normal hardware access has one owner. A configuration request has priority over
 * deferred IRQ work, which has priority over new TX-start entries. A TX-start caller
 * that cannot enter leaves a persistent retry bit. Releasing owners report that fact
 * as a dispatch hint, but only a successful TX-start claim consumes it. This keeps the
 * retry responsibility durable across the release-to-dispatch window and across an
 * intervening ordinary IRQ owner.
 *
 * A normal IRQ that loses ownership must mask the complete normal IRQ domain before
 * calling `MarkIrqDeferred()`. The deferred bit is persistent: a releasing owner does
 * not consume it. A successor must claim IRQ ownership, clear the bit, re-mask the
 * complete normal domain with the required barrier, dispatch and clear all latched
 * normal hardware status, restore the authoritative source mask, and only then release
 * IRQ ownership. Re-masking after the claim is required because another owner may have
 * restored enables after the publisher's earlier mask. This closes both same-core
 * level-IRQ starvation and mask/restore handoff races without blocking in the ISR.
 *
 * `IRQ_DEFERRED` carries no terminal payload. Hardware status remains authoritative
 * until the deferred owner scans it. In particular, a losing IRQ must not cache a
 * COMPLETE/ERROR result and publish it after CONFIG has cleared the old hardware
 * generation. If one scan observes both ERROR and COMPLETE, it advances the TX model
 * once: ERROR absorbs COMPLETE for that snapshot.
 *
 * Configuration may temporarily admit one IRQ sub-owner for asynchronous abort
 * completion. The CONFIG owner publishes that admission only after callback-visible
 * state is initialized, closes admission atomically against callback entry, and
 * remains active until the callback has exited.
 *
 * Platform contract:
 * - all ordinary UART, RX-DMA, and TX-DMA IRQs for one instance execute on one target
 *   core at the same preemption priority;
 * - an IRQ claims this gate before reading or interpreting hardware status; a losing
 *   IRQ caches no COMPLETE, ERROR, or abort payload outside the gate;
 * - CONFIG reports hardware quiescence only after the old DMA is stopped, related
 *   status is cleared with the required barrier/readback, and no selected old callback
 *   can enter a later CONFIG admission;
 * - at most one asynchronous abort is outstanding, and its callback leaves the CONFIG
 *   IRQ sub-owner before it dispatches CONFIG resumption.
 *
 * A platform that queues already-interpreted callbacks outside this ownership domain
 * cannot satisfy the last two rules with the admission bit alone and must carry an
 * immutable transaction token in that callback.
 */
class UartHardwareGate
{
 public:
  enum class PendingAction : uint32_t
  {
    NONE = 0U,
    CONFIG = 1U << 0U,
    IRQ_DEFERRED = 1U << 1U,
    TX_START = 1U << 2U,
  };

  void RequestConfig() { state_.fetch_or(CONFIG_PENDING, std::memory_order_release); }

  /**
   * @brief Publish masked normal-IRQ work for a later hardware-status dispatch.
   * @warning The platform must mask the complete normal IRQ domain, then execute the
   * required device/bus barrier before calling this function.
   * @warning This publishes only a rescan obligation. Do not attach a cached terminal
   * result to it; the eventual owner must read current authoritative hardware status.
   */
  void MarkIrqDeferred()
  {
    state_.fetch_or(IRQ_DEFERRED_PENDING, std::memory_order_release);
  }

  [[nodiscard]] bool TryEnterIrq()
  {
    // A normal source handler may enter while IRQ_DEFERRED is set (for example, an
    // already-pending vector). It must leave the deferred fact intact; only
    // TryEnterDeferredIrq() may consume it and run the complete-domain rescan.
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while ((observed & (OWNER_MASK | CONFIG_PENDING)) == 0U)
    {
      if (state_.compare_exchange_weak(observed, observed | IRQ_ACTIVE,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] PendingAction LeaveIrq() { return LeaveOwner(IRQ_ACTIVE); }

  /**
   * @brief Claim previously masked/deferred normal IRQ work.
   *
   * A successful caller owns the complete normal IRQ domain and must scan all related
   * UART/RX-DMA/TX-DMA status before restoring normal sources. It must re-mask the
   * complete domain after this claim and before reading status; the durable bit does not
   * prove that a publisher's earlier mask still wins. CONFIG_PENDING prevents this
   * claim so configuration can absorb stale hardware state first.
   */
  [[nodiscard]] bool TryEnterDeferredIrq()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (((observed & IRQ_DEFERRED_PENDING) != 0U) &&
           ((observed & (OWNER_MASK | CONFIG_PENDING)) == 0U))
    {
      const uint32_t desired = (observed & ~IRQ_DEFERRED_PENDING) | IRQ_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool TryEnterTxStart()
  {
    uint32_t observed =
        state_.fetch_or(TX_START_PENDING, std::memory_order_release) | TX_START_PENDING;
    while ((observed & (OWNER_MASK | CONFIG_PENDING | IRQ_DEFERRED_PENDING)) == 0U)
    {
      const uint32_t desired = (observed & ~TX_START_PENDING) | TX_START_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] PendingAction LeaveTxStart() { return LeaveOwner(TX_START_ACTIVE); }

  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (((observed & CONFIG_PENDING) != 0U) && ((observed & OWNER_MASK) == 0U))
    {
      const uint32_t desired = (observed & ~CONFIG_PENDING) | CONFIG_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool TryEnterConfigIrq()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (((observed & (CONFIG_ACTIVE | CONFIG_IRQ_ADMITTED)) ==
            (CONFIG_ACTIVE | CONFIG_IRQ_ADMITTED)) &&
           ((observed & IRQ_ACTIVE) == 0U))
    {
      if (state_.compare_exchange_weak(observed, observed | IRQ_ACTIVE,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Publish asynchronous CONFIG IRQ admission after callback state is ready.
   *
   * The release operation pairs with `TryEnterConfigIrq()`'s acquire claim. The
   * admission remains open until `TryLeaveConfig()` starts closing the transaction.
   */
  void OpenConfigIrqAdmission()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      ASSERT((observed & CONFIG_IRQ_ADMITTED) == 0U);
      if (state_.compare_exchange_weak(observed, observed | CONFIG_IRQ_ADMITTED,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  void LeaveConfigIrq()
  {
    const uint32_t observed = state_.fetch_sub(IRQ_ACTIVE, std::memory_order_release);
    ASSERT((observed & CONFIG_ACTIVE) != 0U);
    ASSERT((observed & IRQ_ACTIVE) != 0U);
  }

  void ConsumePendingConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      if (state_.compare_exchange_weak(observed, observed & ~CONFIG_PENDING,
                                       std::memory_order_acq_rel,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /**
   * @brief Try to close the outer configuration transaction.
   * @param actions Receives pending work only when CONFIG ownership was released.
   * This operation first closes CONFIG IRQ admission. Its CAS competes directly with
   * callback entry: either the close wins and no new callback may enter, or the
   * callback wins and CONFIG ownership is retained until that sub-owner exits.
   *
   * @return false if an asynchronous CONFIG IRQ sub-owner is still active or won the
   * entry race; true after CONFIG ownership was released.
   */
  [[nodiscard]] bool TryLeaveConfig(PendingAction& actions)
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      if ((observed & IRQ_ACTIVE) != 0U)
      {
        const uint32_t desired = observed & ~CONFIG_IRQ_ADMITTED;
        if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                         std::memory_order_relaxed))
        {
          actions = PendingAction::NONE;
          return false;
        }
        continue;
      }
      const uint32_t desired = observed & ~(CONFIG_ACTIVE | CONFIG_IRQ_ADMITTED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_relaxed))
      {
        actions = PendingActions(observed);
        return true;
      }
    }
  }

  /**
   * @brief Close CONFIG, atomically closing abort admission against callback entry.
   *
   * This asserting helper is valid only when no CONFIG IRQ sub-owner is active. Use
   * `TryLeaveConfig()` when asynchronous callback completion may still be in flight.
   */
  [[nodiscard]] PendingAction LeaveConfig()
  {
    PendingAction actions = PendingAction::NONE;
    const bool released = TryLeaveConfig(actions);
    ASSERT(released);
    return actions;
  }

  [[nodiscard]] bool ConfigRequested() const
  {
    return (state_.load(std::memory_order_acquire) & (CONFIG_PENDING | CONFIG_ACTIVE)) !=
           0U;
  }

  [[nodiscard]] bool IrqActive() const
  {
    return (state_.load(std::memory_order_acquire) & IRQ_ACTIVE) != 0U;
  }

  [[nodiscard]] bool TxStartActive() const
  {
    return (state_.load(std::memory_order_acquire) & TX_START_ACTIVE) != 0U;
  }

  [[nodiscard]] bool ConfigActive() const
  {
    return (state_.load(std::memory_order_acquire) & CONFIG_ACTIVE) != 0U;
  }

  [[nodiscard]] bool IrqDeferred() const
  {
    return (state_.load(std::memory_order_acquire) & IRQ_DEFERRED_PENDING) != 0U;
  }

  [[nodiscard]] static constexpr bool HasAction(PendingAction actions,
                                                PendingAction action)
  {
    return (static_cast<uint32_t>(actions) & static_cast<uint32_t>(action)) != 0U;
  }

  UartHardwareGate() = default;
  UartHardwareGate(const UartHardwareGate&) = delete;
  UartHardwareGate& operator=(const UartHardwareGate&) = delete;

 private:
  static constexpr uint32_t IRQ_ACTIVE = 1U << 0U;
  static constexpr uint32_t TX_START_ACTIVE = 1U << 1U;
  static constexpr uint32_t CONFIG_ACTIVE = 1U << 2U;
  static constexpr uint32_t CONFIG_PENDING = 1U << 3U;
  static constexpr uint32_t TX_START_PENDING = 1U << 4U;
  static constexpr uint32_t IRQ_DEFERRED_PENDING = 1U << 5U;
  static constexpr uint32_t CONFIG_IRQ_ADMITTED = 1U << 6U;
  static constexpr uint32_t OWNER_MASK = IRQ_ACTIVE | TX_START_ACTIVE | CONFIG_ACTIVE;

  [[nodiscard]] static constexpr PendingAction PendingActions(uint32_t state)
  {
    uint32_t actions = 0U;
    if ((state & CONFIG_PENDING) != 0U)
    {
      actions |= static_cast<uint32_t>(PendingAction::CONFIG);
    }
    if ((state & IRQ_DEFERRED_PENDING) != 0U)
    {
      actions |= static_cast<uint32_t>(PendingAction::IRQ_DEFERRED);
    }
    if ((state & TX_START_PENDING) != 0U)
    {
      actions |= static_cast<uint32_t>(PendingAction::TX_START);
    }
    return static_cast<PendingAction>(actions);
  }

  [[nodiscard]] PendingAction LeaveOwner(uint32_t owner)
  {
    const uint32_t observed = state_.fetch_sub(owner, std::memory_order_release);
    ASSERT((observed & owner) != 0U);
    return PendingActions(observed);
  }

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
