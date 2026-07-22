#pragma once

#include <cstdint>
#include <utility>

#include "serialized_service.hpp"

namespace LibXR
{

/**
 * @brief Per-UART serialized execution without IRQ-domain admission.
 *
 * Direct means that vendor IRQ/HAL code remains unchanged. WRITE, COMPLETE, ERROR, and
 * CONFIG still share this policy's single `SerializedService` owner.
 */
class UartDirectPolicy
{
 public:
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return service_.Invoke(events,
                           [this, &handler](uint32_t snapshot) noexcept
                           {
                             const uint32_t continuation = handler(snapshot);
                             service_.Publish(continuation);
                           });
  }

  /**
   * @brief Publish CONFIG without running hardware control from an arbitrary ISR.
   *
   * A Direct backend cannot know whether that ISR interrupted vendor UART/DMA handling
   * before its LibXR callback. ISR publication is therefore durable but deferred until
   * the next WRITE, UART/DMA callback, or rejected RX carrier enters `Invoke()`.
   */
  template <typename Handler>
  bool InvokeConfig(uint32_t events, bool in_isr, Handler&& handler) noexcept
  {
    if (in_isr)
    {
      service_.Publish(events);
      return false;
    }
    return Invoke(events, std::forward<Handler>(handler));
  }

  /**
   * @brief Read/ack one single-core IRQ source, then publish its service events.
   *
   * Direct backends do not need raw-IRQ admission because the IRQ and normal caller
   * cannot execute simultaneously on different cores. Reading the level source first
   * also prevents an owner-preempting IRQ from repeatedly retriggering while the owner
   * is unable to resume.
   */
  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    Source& source_ref = source;
    const uint32_t events = source_ref();
    if (events == 0U)
    {
      return false;
    }
    return Invoke(events, handler);
  }

  UartDirectPolicy() = default;
  UartDirectPolicy(const UartDirectPolicy&) = delete;
  UartDirectPolicy& operator=(const UartDirectPolicy&) = delete;

 private:
  SerializedService service_{};
};

/**
 * @brief Per-UART serialized execution with SMP IRQ-domain admission.
 *
 * Normal callers serialize the complete IRQ-domain mask with owner admission. A
 * LibXR/application-owned raw ISR uses `InvokeIrq()` so it acquires the same service
 * owner before reading or acknowledging protected status. The no-new-event release CAS
 * and complete-domain restore share the matching short backend guard, so a stale restore
 * cannot reopen one source between a newer mask and owner claim.
 *
 * @tparam Adapter Backend providing `LockAndMaskIrqDomain()`, `UnlockIrqDomain()`,
 * `LockIrqDomain()`, and `RestoreAndUnlockIrqDomain()` methods.
 */
template <typename Adapter>
class UartIrqSerializedPolicy
{
 public:
  explicit UartIrqSerializedPolicy(Adapter& adapter) : adapter_(adapter) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return service_.InvokeGuarded(events, adapter_,
                                  [this, &handler](uint32_t snapshot) noexcept
                                  {
                                    const uint32_t continuation = handler(snapshot);
                                    service_.Publish(continuation);
                                  });
  }

  /** CONFIG already has raw-IRQ exclusion, so every caller may acquire immediately. */
  template <typename Handler>
  bool InvokeConfig(uint32_t events, bool, Handler&& handler) noexcept
  {
    return Invoke(events, std::forward<Handler>(handler));
  }

  /**
   * @brief Admit a raw IRQ source before its first protected status access.
   * @tparam Source Callable with signature `uint32_t()` that reads/acknowledges the
   * protected source and returns service events.
   * @tparam Handler Callable with signature `uint32_t(uint32_t)`.
   */
  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return service_.ClaimAndInvokeGuarded(adapter_, std::forward<Source>(source),
                                          [this, &handler](uint32_t snapshot) noexcept
                                          {
                                            const uint32_t continuation =
                                                handler(snapshot);
                                            service_.Publish(continuation);
                                          });
  }

  UartIrqSerializedPolicy(const UartIrqSerializedPolicy&) = delete;
  UartIrqSerializedPolicy& operator=(const UartIrqSerializedPolicy&) = delete;

 private:
  Adapter& adapter_;
  SerializedService service_{};
};

}  // namespace LibXR
