#pragma once

#include <ti/driverlib/dl_dma.h>

#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR::MSPM0DmaDispatcher
{

/**
 * @brief Shared DMA-vector integration contract.
 *
 * By default `mspm0_dma_dispatcher.cpp` defines `DMA_IRQHandler` and owns the shared DMA
 * vector's NVIC enable state. Every enabled DMA cause in the complete BSP must therefore
 * reach this dispatcher; registered causes are delivered and unregistered causes are
 * reported by the diagnostics below.
 *
 * A BSP that defines `LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER` must define it consistently
 * for the dispatcher translation unit, provide the single shared-vector handler, manage
 * its NVIC state, and call `Dispatch()` on every shared DMA IRQ. Calling it only for a
 * selected channel can strand another registered owner.
 */

using EventMask = uint32_t;

/** @brief Logical DMA events delivered to one channel owner. */
enum Event : EventMask
{
  COMPLETE = 1U << 0U,
  EARLY = 1U << 1U,
  ERROR = 1U << 2U,
};

#if defined(LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK)
inline constexpr uint32_t EARLY_CHANNEL_MASK = LIBXR_MSPM0_DMA_EARLY_CHANNEL_MASK;
#elif defined(ti_devices_msp_m0p_mspm0g351x__include)
inline constexpr uint32_t EARLY_CHANNEL_MASK = 0x3FU;
#else
inline constexpr uint32_t EARLY_CHANNEL_MASK = 0U;
#endif

/** @brief Return whether a hardware channel implements DMA Pre-IRQ. */
[[nodiscard]] constexpr bool EarlyInterruptSupported(uint8_t channel)
{
  return channel < 32U && (EARLY_CHANNEL_MASK & (1UL << channel)) != 0U;
}

using Callback = void (*)(void* context, EventMask events);

class Registration;

/**
 * @brief Register one logical owner for a DMA channel without enabling causes.
 * @param channel DMA channel number
 * @param events Logical causes this owner may enable
 * @param callback Bounded ISR callback invoked with maskable interrupts disabled; it
 *                 must not mutate or recursively dispatch this broker
 * @param context Opaque callback context
 * @param out Empty registration token populated on success
 */
ErrorCode Register(uint8_t channel, EventMask events, Callback callback, void* context,
                   Registration& out);

/**
 * @brief Enable or disable subscribed causes for one live registration.
 * @note The caller must quiesce the corresponding DMA producer when disabling causes.
 */
ErrorCode SetEnabled(Registration& registration, EventMask events, bool enabled);

/**
 * @brief Release a quiescent registration and invalidate its token.
 * @pre No DMA transfer, callback, or concurrent broker operation uses this token.
 */
ErrorCode Unregister(Registration& registration);

/** @brief Dispatch all currently enabled and broker-owned DMA causes. */
void Dispatch();

/** @brief Return the raw completion bit for a hardware channel. */
[[nodiscard]] constexpr uint32_t CompleteMask(uint8_t channel)
{
  return channel < 16U ? (1UL << channel) : 0U;
}

/** @brief Return the raw early-interrupt bit for a hardware channel. */
[[nodiscard]] constexpr uint32_t EarlyMask(uint8_t channel)
{
  return channel < 8U ? (1UL << (16U + channel)) : 0U;
}

/** @brief Return the shared raw address/data error mask. */
[[nodiscard]] constexpr uint32_t ErrorMask()
{
  return DL_DMA_INTERRUPT_ADDR_ERROR | DL_DMA_INTERRUPT_DATA_ERROR;
}

/** @brief Raw unclaimed enabled causes observed by the most recent dispatch. */
[[nodiscard]] uint32_t GetLastUnclaimedMask();

/** @brief Number of dispatches that observed at least one unclaimed enabled cause. */
[[nodiscard]] uint32_t GetUnclaimedCount();

/** @brief Number of dispatches that exhausted the four-pass owned-cause drain bound. */
[[nodiscard]] uint32_t GetDrainLimitCount();

/** @brief Atomically reset dispatcher diagnostics. */
void ResetDiagnostics();

/** @brief Non-copyable capability token for one channel registration. */
class Registration
{
 public:
  Registration() = default;
  Registration(const Registration&) = delete;
  Registration& operator=(const Registration&) = delete;
  Registration(Registration&&) = delete;
  Registration& operator=(Registration&&) = delete;

  [[nodiscard]] bool IsValid() const { return generation_ != 0U; }

 private:
  uint8_t channel_ = 0xFFU;
  uint32_t generation_ = 0U;

  friend ErrorCode Register(uint8_t channel, EventMask events, Callback callback,
                            void* context, Registration& out);
  friend ErrorCode SetEnabled(Registration& registration, EventMask events, bool enabled);
  friend ErrorCode Unregister(Registration& registration);
};

}  // namespace LibXR::MSPM0DmaDispatcher
