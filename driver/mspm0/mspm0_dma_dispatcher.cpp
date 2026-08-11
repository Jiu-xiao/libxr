#include "mspm0_dma_dispatcher.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace LibXR::MSPM0DmaDispatcher
{
namespace
{

constexpr EventMask ALL_EVENTS = COMPLETE | EARLY | ERROR;
// DMA MIS uses bits 0-15 for completion, 16-23 for Pre-IRQ, and 24-25 for errors.
constexpr uint32_t ALL_RAW_CAUSES = 0x03FFFFFFU;
// Bound ISR residency when a callback or peripheral immediately asserts another cause.
constexpr size_t MAX_DRAIN_PASSES = 4U;

static_assert(DMA_SYS_N_DMA_CHANNEL <= 16U,
              "MSPM0 DMA dispatcher raw completion mask supports channels 0-15");

struct Slot
{
  Callback callback_ = nullptr;
  void* context_ = nullptr;
  EventMask subscribed_events_ = 0U;
  EventMask enabled_events_ = 0U;
};

struct Invocation
{
  Callback callback_ = nullptr;
  void* context_ = nullptr;
  EventMask events_ = 0U;
};

// Serializes task and maskable-exception access on one core; it does not cover NMI or
// SMP.
class InterruptGuard
{
 public:
  InterruptGuard() : primask_(__get_PRIMASK())
  {
    __disable_irq();
    __DMB();
  }

  ~InterruptGuard()
  {
    __DSB();
    __set_PRIMASK(primask_);
    __ISB();
  }

  InterruptGuard(const InterruptGuard&) = delete;
  InterruptGuard& operator=(const InterruptGuard&) = delete;

 private:
  uint32_t primask_;
};

std::array<Slot, DMA_SYS_N_DMA_CHANNEL> slots{};
std::atomic<uint32_t> enabled_raw_mask{0U};
std::atomic<uint32_t> last_unclaimed_mask{0U};
std::atomic<uint32_t> unclaimed_count{0U};
std::atomic<uint32_t> drain_limit_count{0U};

[[nodiscard]] constexpr bool EventsAreValid(EventMask events)
{
  return events != 0U && (events & ~ALL_EVENTS) == 0U;
}

[[nodiscard]] bool TokenMatches(uint8_t channel)
{
  if (channel >= slots.size())
  {
    return false;
  }

  return slots[channel].callback_ != nullptr;
}

[[nodiscard]] bool AnyErrorSubscriberEnabled()
{
  for (const Slot& slot : slots)
  {
    if ((slot.enabled_events_ & ERROR) != 0U)
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool AnySlotRegistered()
{
  for (const Slot& slot : slots)
  {
    if (slot.callback_ != nullptr)
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] uint32_t RecomputeEnabledRawMask()
{
  uint32_t raw_mask = 0U;
  for (uint8_t channel = 0U; channel < slots.size(); ++channel)
  {
    const EventMask events = slots[channel].enabled_events_;
    if ((events & COMPLETE) != 0U)
    {
      raw_mask |= CompleteMask(channel);
    }
    if ((events & EARLY) != 0U)
    {
      raw_mask |= EarlyMask(channel);
    }
    if ((events & ERROR) != 0U)
    {
      raw_mask |= ErrorMask();
    }
  }
  return raw_mask;
}

[[nodiscard]] uint32_t ChannelRawMask(uint8_t channel, EventMask events)
{
  uint32_t raw_mask = 0U;
  if ((events & COMPLETE) != 0U)
  {
    raw_mask |= CompleteMask(channel);
  }
  if ((events & EARLY) != 0U)
  {
    raw_mask |= EarlyMask(channel);
  }
  return raw_mask;
}

void PublishEnabledRawMask()
{
  enabled_raw_mask.store(RecomputeEnabledRawMask(), std::memory_order_release);
}

}  // namespace

ErrorCode Register(uint8_t channel, EventMask events, Callback callback, void* context,
                   Registration& out)
{
  if (!EventsAreValid(events) || callback == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }
  if (channel >= slots.size())
  {
    return ErrorCode::OUT_OF_RANGE;
  }
  if ((events & EARLY) != 0U && !EarlyInterruptSupported(channel))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  bool first_registration = false;
  {
    InterruptGuard guard;
    if (out.channel_ != 0xFFU)
    {
      return ErrorCode::STATE_ERR;
    }
    if (slots[channel].callback_ != nullptr)
    {
      return ErrorCode::BUSY;
    }

    first_registration = !AnySlotRegistered();
    Slot& slot = slots[channel];
    slot.context_ = context;
    slot.subscribed_events_ = events;
    slot.enabled_events_ = 0U;
    slot.callback_ = callback;
    out.channel_ = channel;
  }
#if !defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  if (first_registration)
  {
    InterruptGuard guard;
    if (NVIC_GetEnableIRQ(DMA_INT_IRQn) == 0U)
    {
      NVIC_EnableIRQ(DMA_INT_IRQn);
    }
  }
#else
  static_cast<void>(first_registration);
#endif
  return ErrorCode::OK;
}

ErrorCode SetEnabled(const Registration& registration, EventMask events, bool enabled)
{
  if (!EventsAreValid(events))
  {
    return ErrorCode::ARG_ERR;
  }

  InterruptGuard guard;
  if (!TokenMatches(registration.channel_))
  {
    return ErrorCode::STATE_ERR;
  }

  Slot& slot = slots[registration.channel_];
  if ((events & ~slot.subscribed_events_) != 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  const EventMask changed_events =
      enabled ? (events & ~slot.enabled_events_) : (events & slot.enabled_events_);
  if (changed_events == 0U)
  {
    return ErrorCode::OK;
  }

  const bool had_error_subscriber = AnyErrorSubscriberEnabled();
  const uint32_t channel_raw_mask = ChannelRawMask(registration.channel_, changed_events);

  if (enabled)
  {
    slot.enabled_events_ |= changed_events;
    if (channel_raw_mask != 0U)
    {
      DL_DMA_clearInterruptStatus(DMA, channel_raw_mask);
      DL_DMA_enableInterrupt(DMA, channel_raw_mask);
    }
    if ((changed_events & ERROR) != 0U && !had_error_subscriber)
    {
      DL_DMA_clearInterruptStatus(DMA, ErrorMask());
      DL_DMA_enableInterrupt(DMA, ErrorMask());
    }
  }
  else
  {
    if (channel_raw_mask != 0U)
    {
      DL_DMA_disableInterrupt(DMA, channel_raw_mask);
      DL_DMA_clearInterruptStatus(DMA, channel_raw_mask);
    }
    slot.enabled_events_ &= ~changed_events;
    if ((changed_events & ERROR) != 0U && !AnyErrorSubscriberEnabled())
    {
      DL_DMA_disableInterrupt(DMA, ErrorMask());
      DL_DMA_clearInterruptStatus(DMA, ErrorMask());
    }
  }

  PublishEnabledRawMask();
  return ErrorCode::OK;
}

void Dispatch()
{
  uint32_t observed_unclaimed = 0U;
  bool exhausted = true;

  for (size_t pass = 0U; pass < MAX_DRAIN_PASSES; ++pass)
  {
    std::array<Invocation, DMA_SYS_N_DMA_CHANNEL> invocations{};
    size_t invocation_count = 0U;
    uint32_t snapshot = 0U;

    {
      InterruptGuard guard;
      const uint32_t owned_raw_mask = enabled_raw_mask.load(std::memory_order_acquire);
      const uint32_t all_mis = DL_DMA_getEnabledInterruptStatus(DMA, ALL_RAW_CAUSES);
      observed_unclaimed |= all_mis & ~owned_raw_mask;
      snapshot = all_mis & owned_raw_mask;
      if (snapshot != 0U)
      {
        // Clear captured bits before callbacks so callback-time reassertions remain
        // pending.
        DL_DMA_clearInterruptStatus(DMA, snapshot);
        __DMB();

        for (uint8_t channel = 0U; channel < slots.size(); ++channel)
        {
          const Slot& slot = slots[channel];
          EventMask logical_events = 0U;
          if ((slot.enabled_events_ & COMPLETE) != 0U &&
              (snapshot & CompleteMask(channel)) != 0U)
          {
            logical_events |= COMPLETE;
          }
          if ((slot.enabled_events_ & EARLY) != 0U &&
              (snapshot & EarlyMask(channel)) != 0U)
          {
            logical_events |= EARLY;
          }
          if ((slot.enabled_events_ & ERROR) != 0U && (snapshot & ErrorMask()) != 0U)
          {
            logical_events |= ERROR;
          }
          if (logical_events != 0U && slot.callback_ != nullptr)
          {
            invocations[invocation_count++] =
                Invocation{slot.callback_, slot.context_, logical_events};
          }
        }

        // Keep the slot snapshot and callbacks in one critical section so registration
        // state cannot change between capture and invocation.
        for (size_t i = 0U; i < invocation_count; ++i)
        {
          invocations[i].callback_(invocations[i].context_, invocations[i].events_);
        }
      }
    }

    if (snapshot == 0U)
    {
      exhausted = false;
      break;
    }
  }

  {
    InterruptGuard guard;
    const uint32_t owned_raw_mask = enabled_raw_mask.load(std::memory_order_acquire);
    const uint32_t remaining = DL_DMA_getEnabledInterruptStatus(DMA, ALL_RAW_CAUSES);
    observed_unclaimed |= remaining & ~owned_raw_mask;
    if (exhausted && (remaining & owned_raw_mask) != 0U)
    {
      drain_limit_count.fetch_add(1U, std::memory_order_relaxed);
    }
  }

  last_unclaimed_mask.store(observed_unclaimed, std::memory_order_relaxed);
  if (observed_unclaimed != 0U)
  {
    unclaimed_count.fetch_add(1U, std::memory_order_relaxed);
  }
  // Complete peripheral W1C writes before exception return so the NVIC resamples the
  // line.
  __DSB();
}

uint32_t GetLastUnclaimedMask()
{
  return last_unclaimed_mask.load(std::memory_order_relaxed);
}

uint32_t GetUnclaimedCount() { return unclaimed_count.load(std::memory_order_relaxed); }

uint32_t GetDrainLimitCount()
{
  return drain_limit_count.load(std::memory_order_relaxed);
}

}  // namespace LibXR::MSPM0DmaDispatcher

#if !defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void DMA_IRQHandler(void) { LibXR::MSPM0DmaDispatcher::Dispatch(); }
#endif
