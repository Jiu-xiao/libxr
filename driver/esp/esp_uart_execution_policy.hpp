#pragma once

#include <cstdint>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "uart/uart_execution_policy.hpp"

namespace LibXR::Detail
{

#if (SOC_CPU_CORES_NUM > 1) && \
    (!defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE)
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = true;
#else
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = false;
#endif

#if defined(CONFIG_APPTRACE_SV_ENABLE) && CONFIG_APPTRACE_SV_ENABLE
static_assert(!ESP_UART_USES_IRQ_SERIALIZATION,
              "ESP UART SMP IRQ serialization is incompatible with the ESP-IDF "
              "SystemView interrupt wrapper");
#endif

/**
 * @brief Connect one ESP UART backend's IRQ domain to the common serialized policy.
 *
 * The owner supplies the IRQ-domain lock and the locked interrupt enable/disable hook.
 * Related interrupts must be fixed to one core and allocated as non-shared sources.
 */
template <typename Owner>
class ESP32UartIrqAdapter
{
 public:
  explicit ESP32UartIrqAdapter(Owner& owner) : owner_(owner) {}

  void LockAndMaskIrqDomain() noexcept
  {
    portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_);
    owner_.SetIrqDomainEnabledLocked(false);
  }

  void UnlockIrqDomain() noexcept { portEXIT_CRITICAL_SAFE(&owner_.irq_domain_lock_); }

  void LockIrqDomain() noexcept { portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_); }

  void RestoreAndUnlockIrqDomain() noexcept
  {
    owner_.SetIrqDomainEnabledLocked(true);
    portEXIT_CRITICAL_SAFE(&owner_.irq_domain_lock_);
  }

 private:
  Owner& owner_;
};

template <typename Owner, bool UseIrqSerialization = ESP_UART_USES_IRQ_SERIALIZATION>
class ESP32UartExecutionPolicy;

template <typename Owner>
class ESP32UartExecutionPolicy<Owner, false>
{
 public:
  explicit ESP32UartExecutionPolicy(Owner&) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  UartDirectPolicy policy_{};
};

template <typename Owner>
class ESP32UartExecutionPolicy<Owner, true>
{
 public:
  explicit ESP32UartExecutionPolicy(Owner& owner) : adapter_(owner), policy_(adapter_) {}

  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  ESP32UartIrqAdapter<Owner> adapter_;
  UartIrqSerializedPolicy<ESP32UartIrqAdapter<Owner>> policy_;
};

}  // namespace LibXR::Detail
