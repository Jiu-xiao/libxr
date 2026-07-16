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
 * as a dispatch hint. A successful TX-start claim consumes it; a successful CONFIG
 * claim retires a retry from the old hardware generation because the TX model will
 * destructively drain that fixed prefix and unconditionally rescan post-boundary writes.
 * This keeps the retry responsibility durable across ordinary release-to-dispatch
 * windows without leaving stale work after CONFIG.
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
 * CONFIG is a highest-priority level-triggered request. `RequestConfig()` may be
 * called from any UART entry. Whichever later entry successfully claims CONFIG retains
 * `CONFIG_ACTIVE` until the backend has quiesced the old hardware generation, applied
 * the accepted payload, and completed its software drain. The CPU call stack may return
 * while that ownership remains active. This gate does not identify or admit asynchronous
 * abort IRQs; a backend that needs them must provide direction-specific admission while
 * continuing to exclude every ordinary IRQ and TX start.
 *
 * Platform contract:
 * - all ordinary UART, RX-DMA, and TX-DMA IRQs for one instance execute on one target
 *   core at the same preemption priority;
 * - an IRQ claims this gate before reading or interpreting hardware status; a losing
 *   IRQ caches no COMPLETE, ERROR, or abort payload outside the gate;
 * - CONFIG reports hardware quiescence only after the old DMA is stopped, related
 *   status is cleared with the required barrier/readback, and every selected old abort
 *   callback has exited before a later TX start or CONFIG admission;
 * - CONFIG apply is destructive for the old hardware generation: it stops old RX/TX
 *   activity, clears old terminal sources, applies the latest payload, and restarts the
 *   accepted hardware state before releasing CONFIG ownership.
 * - the TX retry bit represents the single candidate owned by one serialized TX model;
 *   it is not a counter for independent TX-start callers.
 */
class UartHardwareGate
{
 public:
  /**
   * @brief Synchronous ownership token for one ordinary/deferred IRQ call stack.
   *
   * The token is stack-local ownership evidence, not an event payload. It may be passed
   * through a synchronously executing TX service owner, but must never be copied, stored,
   * or used after the outer IRQ owner leaves the gate. The non-atomic depth is touched
   * only by that owning call stack.
   */
  class OwnerContext
  {
   public:
    OwnerContext() = default;
    OwnerContext(const OwnerContext&) = delete;
    OwnerContext& operator=(const OwnerContext&) = delete;
    OwnerContext(OwnerContext&&) = delete;
    OwnerContext& operator=(OwnerContext&&) = delete;

   private:
    friend class UartHardwareGate;

    UartHardwareGate* gate_ = nullptr;
    uint32_t depth_ = 0U;
  };

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

  [[nodiscard]] bool TryEnterIrq(OwnerContext& context)
  {
    if (!ContextIsUnused(context))
    {
      ASSERT(false);
      return false;
    }
    if (!TryEnterIrq())
    {
      return false;
    }
    BindIrqContext(context);
    return true;
  }

  [[nodiscard]] PendingAction LeaveIrq() { return LeaveOwner(IRQ_ACTIVE); }

  [[nodiscard]] PendingAction LeaveIrq(OwnerContext& context)
  {
    if (!ContextIsOuterIrq(context))
    {
      ASSERT(false);
      return PendingAction::NONE;
    }
    const PendingAction actions = LeaveIrq();
    ResetContext(context);
    return actions;
  }

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

  [[nodiscard]] bool TryEnterDeferredIrq(OwnerContext& context)
  {
    if (!ContextIsUnused(context))
    {
      ASSERT(false);
      return false;
    }
    if (!TryEnterDeferredIrq())
    {
      return false;
    }
    BindIrqContext(context);
    return true;
  }

  /**
   * @brief Atomically admit one TX start nested under the current IRQ owner.
   *
   * The successful CAS is the priority linearization point. CONFIG or deferred IRQ work
   * published first forces a durable TX retry; publication after the CAS waits for the
   * outer IRQ owner to leave. Successful nesting keeps IRQ_ACTIVE as the sole global
   * owner and changes only the stack-local depth.
   */
  [[nodiscard]] bool TryEnterNestedTxStart(OwnerContext& context)
  {
    if (!ContextIsOuterIrq(context))
    {
      ASSERT(false);
      return false;
    }

    // A valid outer context implies that the common state is exactly IRQ_ACTIVE. Start
    // the CAS from that value so uncontended nested admission needs no separate load;
    // failure returns any concurrently published pending bits in observed.
    uint32_t observed = IRQ_ACTIVE;
    while ((observed & OWNER_MASK) == IRQ_ACTIVE)
    {
      if ((observed & (CONFIG_PENDING | IRQ_DEFERRED_PENDING)) != 0U)
      {
        state_.fetch_or(TX_START_PENDING, std::memory_order_release);
        return false;
      }

      const uint32_t desired = observed & ~TX_START_PENDING;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        context.depth_ = 2U;
        return true;
      }
    }

    ASSERT(false);
    return false;
  }

  /**
   * @brief Leave a TX start nested under an IRQ owner without releasing the gate.
   *
   * Pending work is dispatched only when the outer IRQ owner leaves. This mirrors a
   * nested critical section: inner leave restores the outer depth and performs no atomic
   * owner transition.
   */
  void LeaveNestedTxStart(OwnerContext& context)
  {
    if ((context.gate_ != this) || (context.depth_ != 2U))
    {
      ASSERT(false);
      return;
    }
    context.depth_ = 1U;
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

  /**
   * @brief Claim CONFIG and retire a TX retry from its fixed old prefix.
   *
   * The caller must fix the model's CONFIG boundary before this claim. The successful
   * CAS is also the retry-retirement linearization point: an earlier TX retry belongs to
   * the destructive old prefix, while a retry published after the CAS remains pending.
   * The TX model must rescan its authoritative pending/queue state after CONFIG.
   */
  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (((observed & CONFIG_PENDING) != 0U) && ((observed & OWNER_MASK) == 0U))
    {
      const uint32_t desired =
          (observed & ~(CONFIG_PENDING | TX_START_PENDING)) | CONFIG_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
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

  [[nodiscard]] PendingAction LeaveConfig() { return LeaveOwner(CONFIG_ACTIVE); }

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
    // Nested TX start remains covered by IRQ_ACTIVE and is intentionally not reported.
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
  static constexpr uint32_t OWNER_MASK = IRQ_ACTIVE | TX_START_ACTIVE | CONFIG_ACTIVE;

  [[nodiscard]] static bool ContextIsUnused(const OwnerContext& context)
  {
    return (context.gate_ == nullptr) && (context.depth_ == 0U);
  }

  [[nodiscard]] bool ContextIsOuterIrq(const OwnerContext& context) const
  {
    return (context.gate_ == this) && (context.depth_ == 1U);
  }

  void BindIrqContext(OwnerContext& context)
  {
    context.gate_ = this;
    context.depth_ = 1U;
  }

  static void ResetContext(OwnerContext& context)
  {
    context.gate_ = nullptr;
    context.depth_ = 0U;
  }

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
