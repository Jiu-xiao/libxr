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
   * @brief Quiesce/read/ack one single-core IRQ source, then publish its events.
   *
   * Direct backends do not need raw-IRQ owner admission because the IRQ and normal
   * caller cannot execute simultaneously on different cores. A condition-triggered
   * source that can immediately reassert must nevertheless be masked or otherwise made
   * one-shot by `source` before publication, then re-armed by `handler` if work remains.
   * Acknowledgement alone is insufficient when the IRQ preempts an existing owner: the
   * handler is deferred, so repeated IRQ entry could prevent that owner from resuming.
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
 * This policy requires control before the SDK/HAL first reads or acknowledges the IRQ
 * source. It cannot provide hardware serialization when it is entered only from a
 * callback that the SDK invokes after handling the source, unless that SDK already
 * provides equivalent cross-core serialization for its IRQ handler and UART/DMA APIs.
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

  /**
   * @brief Admit a raw IRQ source before its first protected status access.
   *
   * `source` must contain the first protected status read/acknowledgement. Calling this
   * method only after an SDK/HAL IRQ handler has touched that status is too late to
   * protect the handler from concurrent hardware operations on another core.
   *
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
