#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Serialize one direct RX producer and short TX admissions against CONFIG and
 * recovery control.
 *
 * RX claims the gate before reading DMA position or descriptor state and before moving
 * bytes into the software queue. An IRQ adapter may read and acknowledge its interrupt
 * status first because CONFIG explicitly permits transition-window RX data to be
 * discarded. CONFIG reservation and runtime recovery both close later RX admission. An
 * RX fragment already in progress may finish; its release reports that the waiting
 * control transaction can be retried.
 *
 * CONFIG reservation and payload publication are separate transitions. This prevents
 * an RX release from waking the CONFIG consumer before the accepted payload is complete.
 *
 * TX claims the gate only while it copies a public queue record into the local pending
 * slot or promotes that pending record to hardware. If CONFIG is already reserved or
 * pending, TX admission fails without touching the public queues. If TX admission wins
 * first, CONFIG may be reserved but cannot advance until the short TX admission leaves;
 * that TX record is then linearly before CONFIG and is handled by the normal
 * active/pending CONFIG rules.
 *
 * Only one CONFIG may be reserved or pending; the pending bit remains set for its entire
 * lifetime, so later requests fail admission until the accepted CONFIG completes. One
 * generic CONTROL_ACTIVE bit covers either CONFIG or recovery across asynchronous stop.
 */
class UartRxConfigGate
{
 public:
  /**
   * @brief Reserve the only CONFIG slot and close RX admission.
   * @return true when reserved; false while another CONFIG is outstanding.
   */
  [[nodiscard]] bool TryReserveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while ((observed & CONFIG_MASK) == 0U)
    {
      const uint32_t desired = observed | CONFIG_RESERVED;
      if (state_.compare_exchange_strong(observed, desired, std::memory_order_acquire,
                                         std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  /** Publish the complete CONFIG payload before notifying the serialized service. */
  void PublishConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_RESERVED) != 0U);
      ASSERT((observed & CONFIG_PENDING) == 0U);
      const uint32_t desired = (observed & ~CONFIG_RESERVED) | CONFIG_PENDING;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /** Claim the direct RX hardware fragment, or return false so the data is dropped. */
  [[nodiscard]] bool TryEnterRx()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & (RX_ACTIVE | CONFIG_MASK | CONTROL_PENDING | CONTROL_ACTIVE)) ==
           0U)
    {
      const uint32_t desired = observed | RX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Claim a short TX staging/start admission before touching TX queue state.
   *
   * A false result means CONFIG has already won the boundary; the caller must not pop
   * payload, promote pending state, or start hardware in this owner snapshot.
   */
  [[nodiscard]] bool TryEnterTx()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_MASK) == 0U)
    {
      ASSERT((observed & TX_ACTIVE) == 0U);
      const uint32_t desired = observed | TX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /** Release a short TX staging/start admission. */
  void LeaveTx()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & TX_ACTIVE) != 0U);
      const uint32_t desired = observed & ~TX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /** Release RX and report whether this caller must publish CONTROL_READY. */
  [[nodiscard]] bool LeaveRx()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & RX_ACTIVE) != 0U);
      const uint32_t desired = observed & ~RX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return (observed & (CONFIG_PENDING | CONTROL_PENDING)) != 0U;
      }
    }
  }

  /** Enter or resume the one serialized CONFIG transaction. */
  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_PENDING) != 0U)
    {
      if ((observed & (RX_ACTIVE | TX_ACTIVE | CONFIG_RESERVED)) != 0U)
      {
        return false;
      }

      const uint32_t desired = (observed | CONTROL_ACTIVE) & ~CONTROL_PENDING;
      if (desired == observed)
      {
        return true;
      }
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /** Complete the only CONFIG transaction and reopen admission. */
  void LeaveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & (CONFIG_PENDING | CONTROL_ACTIVE)) ==
             (CONFIG_PENDING | CONTROL_ACTIVE));
      ASSERT((observed & (RX_ACTIVE | TX_ACTIVE | CONFIG_RESERVED | CONTROL_PENDING)) ==
             0U);
      const uint32_t desired = observed & ~(CONFIG_PENDING | CONTROL_ACTIVE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /** Enter or resume runtime recovery, or leave a durable wait behind active RX. */
  [[nodiscard]] bool TryEnterRecovery()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_MASK) == 0U)
    {
      if ((observed & CONTROL_ACTIVE) != 0U)
      {
        return true;
      }
      if (((observed & RX_ACTIVE) != 0U) && ((observed & CONTROL_PENDING) != 0U))
      {
        return false;
      }

      const uint32_t desired = ((observed & RX_ACTIVE) != 0U)
                                   ? (observed | CONTROL_PENDING)
                                   : ((observed | CONTROL_ACTIVE) & ~CONTROL_PENDING);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return (desired & CONTROL_ACTIVE) != 0U;
      }
    }
    return false;
  }

  /** Complete runtime recovery while preserving any concurrently reserved CONFIG. */
  void LeaveRecovery()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONTROL_ACTIVE) != 0U);
      ASSERT((observed & RX_ACTIVE) == 0U);
      const uint32_t desired = observed & ~(CONTROL_PENDING | CONTROL_ACTIVE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  [[nodiscard]] bool ConfigRequested() const
  {
    return (state_.load(std::memory_order_acquire) & CONFIG_MASK) != 0U;
  }

  UartRxConfigGate() = default;
  UartRxConfigGate(const UartRxConfigGate&) = delete;
  UartRxConfigGate& operator=(const UartRxConfigGate&) = delete;

 private:
  static constexpr uint32_t RX_ACTIVE = 1U << 0U;
  static constexpr uint32_t CONFIG_RESERVED = 1U << 1U;
  static constexpr uint32_t CONFIG_PENDING = 1U << 2U;
  static constexpr uint32_t CONTROL_PENDING = 1U << 3U;
  static constexpr uint32_t CONTROL_ACTIVE = 1U << 4U;
  static constexpr uint32_t TX_ACTIVE = 1U << 5U;
  static constexpr uint32_t CONFIG_MASK = CONFIG_RESERVED | CONFIG_PENDING;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
