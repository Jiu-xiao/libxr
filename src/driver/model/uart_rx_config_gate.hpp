#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Serialize one direct RX producer against CONFIG and recovery control.
 *
 * RX claims the gate before reading DMA position or descriptor state. CONFIG reservation
 * and runtime recovery both close later RX admission. An RX fragment already in progress
 * may finish; its release reports that the waiting control transaction can be retried.
 *
 * CONFIG reservation and payload publication are separate transitions. This prevents
 * an RX release from waking the CONFIG consumer before the accepted payload is complete.
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

  /** Claim the direct RX hardware fragment, or return false so the event is dropped. */
  [[nodiscard]] bool TryEnterRx()
  {
    uint32_t expected = 0U;
    return state_.compare_exchange_strong(expected, RX_ACTIVE, std::memory_order_acquire,
                                          std::memory_order_relaxed);
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
      if ((observed & (RX_ACTIVE | CONFIG_RESERVED)) != 0U)
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
      ASSERT((observed & (RX_ACTIVE | CONFIG_RESERVED | CONTROL_PENDING)) == 0U);
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
  static constexpr uint32_t CONFIG_MASK = CONFIG_RESERVED | CONFIG_PENDING;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
