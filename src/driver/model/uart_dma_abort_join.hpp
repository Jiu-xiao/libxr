#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Join synchronous or asynchronous TX/RX DMA stop completion.
 *
 * The state word retains three facts: the transaction phase, directions that have not
 * reached a hardware-stopped boundary, and directions whose asynchronous stop IRQ is
 * armed. `QUIESCENT` is published only after the complete launch sequence and every
 * selected direction has been proved stopped.
 *
 * An armed IRQ is only a wakeup for a backend-specific stopped-state check. Calling
 * `FinishAsyncStopIrq(direction, false)` retains both obligations for a later IRQ;
 * only `true` clears them. The backend must finish all access to the old hardware state
 * before making that call.
 */
class UartDmaAbortJoin
{
 public:
  enum class Direction : uint32_t
  {
    NONE = 0U,
    TX = 1U << 0U,
    RX = 1U << 1U,
  };

  enum class Phase : uint32_t
  {
    IDLE = 0U,
    LAUNCHING = 1U,
    STOPPING = 2U,
    QUIESCENT = 3U,
    APPLIED = 4U,
  };

  static constexpr uint32_t Mask(Direction direction)
  {
    return static_cast<uint32_t>(direction);
  }

  static constexpr uint32_t ALL_DIRECTIONS =
      static_cast<uint32_t>(Direction::TX) | static_cast<uint32_t>(Direction::RX);

  /** Publish the full direction set before launching any stop operation. */
  void Begin(uint32_t directions)
  {
    if ((directions & ~ALL_DIRECTIONS) != 0U)
    {
      ASSERT(false);
      return;
    }

    uint32_t expected = EncodePhase(Phase::IDLE);
    const uint32_t desired =
        EncodePhase(Phase::LAUNCHING) | (directions << PENDING_SHIFT);
    if (!state_.compare_exchange_strong(expected, desired, std::memory_order_release,
                                        std::memory_order_relaxed))
    {
      ASSERT(false);
    }
  }

  /**
   * Arm a later IRQ to recheck whether one asynchronous DMA stop has completed.
   * @return false when the direction was already completed synchronously.
   */
  bool ArmAsyncStop(Direction direction)
  {
    const uint32_t pending_bit = PendingBit(direction);
    const uint32_t armed_bit = ArmedBit(direction);
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (IsStopPhase(observed))
    {
      if ((observed & pending_bit) == 0U)
      {
        return false;
      }
      if ((observed & armed_bit) != 0U)
      {
        ASSERT(false);
        return false;
      }
      const uint32_t desired = observed | armed_bit;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }

    ASSERT(false);
    return false;
  }

  /** Complete one unarmed direction after a backend-specific stopped-state proof. */
  bool CompleteStopped(Direction direction)
  {
    const uint32_t pending_bit = PendingBit(direction);
    const uint32_t armed_bit = ArmedBit(direction);
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (IsStopPhase(observed))
    {
      if (((observed & pending_bit) == 0U) || ((observed & armed_bit) != 0U))
      {
        ASSERT(false);
        return false;
      }
      const uint32_t desired = observed & ~pending_bit;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return TryQuiescent();
      }
    }

    ASSERT(false);
    return false;
  }

  /** Close the launch window after every selected stop API call has returned. */
  bool EndLaunch()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (DecodePhase(observed) == Phase::LAUNCHING)
    {
      const uint32_t desired = (observed & ~PHASE_MASK) | EncodePhase(Phase::STOPPING);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return TryQuiescent();
      }
    }

    ASSERT(false);
    return false;
  }

  /**
   * Finish one armed stop IRQ after its authoritative stopped-state check.
   *
   * A false result leaves the direction pending and armed so another terminal IRQ can
   * retry. A true result clears both obligations and may publish `QUIESCENT`.
   */
  bool FinishAsyncStopIrq(Direction direction, bool dma_stopped)
  {
    const uint32_t pending_bit = PendingBit(direction);
    const uint32_t armed_bit = ArmedBit(direction);
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (IsStopPhase(observed))
    {
      if (((observed & pending_bit) == 0U) || ((observed & armed_bit) == 0U))
      {
        ASSERT(false);
        return false;
      }
      if (!dma_stopped)
      {
        return false;
      }

      const uint32_t desired = observed & ~(pending_bit | armed_bit);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return TryQuiescent();
      }
    }

    ASSERT(false);
    return false;
  }

  /** Publish `QUIESCENT` once all launch and stopped-state obligations have ended. */
  bool TryQuiescent()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((DecodePhase(observed) == Phase::STOPPING) &&
           ((observed & OBLIGATION_MASK) == 0U))
    {
      const uint32_t desired = EncodePhase(Phase::QUIESCENT);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  void MarkApplied()
  {
    uint32_t expected = EncodePhase(Phase::QUIESCENT);
    if (!state_.compare_exchange_strong(expected, EncodePhase(Phase::APPLIED),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
    {
      ASSERT(false);
    }
  }

  void Finish()
  {
    uint32_t expected = EncodePhase(Phase::APPLIED);
    if (!state_.compare_exchange_strong(expected, EncodePhase(Phase::IDLE),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
    {
      ASSERT(false);
    }
  }

  [[nodiscard]] Phase GetPhase() const
  {
    return DecodePhase(state_.load(std::memory_order_acquire));
  }

  [[nodiscard]] bool Pending(Direction direction) const
  {
    return (state_.load(std::memory_order_acquire) & PendingBit(direction)) != 0U;
  }

  [[nodiscard]] bool AsyncStopArmed(Direction direction) const
  {
    return (state_.load(std::memory_order_acquire) & ArmedBit(direction)) != 0U;
  }

  [[nodiscard]] bool IsQuiescent() const { return GetPhase() == Phase::QUIESCENT; }

  UartDmaAbortJoin() = default;
  UartDmaAbortJoin(const UartDmaAbortJoin&) = delete;
  UartDmaAbortJoin& operator=(const UartDmaAbortJoin&) = delete;

 private:
  static constexpr uint32_t PHASE_MASK = 0x7U;
  static constexpr uint32_t DIRECTION_MASK = ALL_DIRECTIONS;
  static constexpr uint32_t PENDING_SHIFT = 3U;
  static constexpr uint32_t ARMED_SHIFT = 5U;
  static constexpr uint32_t PENDING_MASK = DIRECTION_MASK << PENDING_SHIFT;
  static constexpr uint32_t ARMED_MASK = DIRECTION_MASK << ARMED_SHIFT;
  static constexpr uint32_t OBLIGATION_MASK = PENDING_MASK | ARMED_MASK;

  static constexpr uint32_t EncodePhase(Phase phase)
  {
    return static_cast<uint32_t>(phase);
  }

  static constexpr Phase DecodePhase(uint32_t state)
  {
    return static_cast<Phase>(state & PHASE_MASK);
  }

  static constexpr bool IsStopPhase(uint32_t state)
  {
    const Phase phase = DecodePhase(state);
    return (phase == Phase::LAUNCHING) || (phase == Phase::STOPPING);
  }

  static constexpr uint32_t DirectionBit(Direction direction)
  {
    const uint32_t direction_bit = Mask(direction);
    return ((direction_bit == Mask(Direction::TX)) ||
            (direction_bit == Mask(Direction::RX)))
               ? direction_bit
               : 0U;
  }

  static uint32_t PendingBit(Direction direction)
  {
    const uint32_t direction_bit = DirectionBit(direction);
    ASSERT(direction_bit != 0U);
    return direction_bit << PENDING_SHIFT;
  }

  static uint32_t ArmedBit(Direction direction)
  {
    const uint32_t direction_bit = DirectionBit(direction);
    ASSERT(direction_bit != 0U);
    return direction_bit << ARMED_SHIFT;
  }

  std::atomic<uint32_t> state_{EncodePhase(Phase::IDLE)};
};

}  // namespace LibXR
