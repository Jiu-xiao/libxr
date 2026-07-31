#pragma once

#include <cstdint>
#include <utility>

#include "esp_def.hpp"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "uart/uart_execution_policy.hpp"

namespace LibXR::Detail
{

/**
 * @brief 当前构建是否需要跨核 raw-IRQ 串行化 / Whether this build needs cross-core
 * raw-IRQ serialization
 */
#if (SOC_CPU_CORES_NUM > 1) && \
    (!defined(CONFIG_FREERTOS_UNICORE) || !CONFIG_FREERTOS_UNICORE)
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = true;
#else
inline constexpr bool ESP_UART_USES_IRQ_SERIALIZATION = false;
#endif

#if defined(CONFIG_APPTRACE_SV_ENABLE) && CONFIG_APPTRACE_SV_ENABLE
// SystemView wrapper 可能持有 interrupt-allocator lock 后调用 ISR，而 SMP adapter 中的
// esp_intr_disable() 会再次获取同一 lock。 / The SystemView wrapper may invoke an ISR
// while holding the interrupt-allocator lock, which esp_intr_disable() reacquires.
static_assert(!ESP_UART_USES_IRQ_SERIALIZATION,
              "ESP UART SMP IRQ serialization is incompatible with the ESP-IDF "
              "SystemView interrupt wrapper");
#endif

/**
 * @brief 将 ESP UART 后端的 IRQ 域接入通用串行策略 / Connect one ESP UART backend's
 * IRQ domain to the common serialized policy
 *
 * owner 提供 IRQ-domain lock，以及持锁状态下的 interrupt enable/disable hook。相关 IRQ
 * 必须固定在同一核心，并分配为 non-shared source。 / The owner supplies the IRQ-domain
 * lock and the locked interrupt enable/disable hook. Related interrupts must be fixed to
 * one core and allocated as non-shared sources.
 *
 * @tparam Owner 持有 IRQ 域资源的 ESP UART 后端 / ESP UART backend owning the IRQ-domain
 * resources
 */
template <typename Owner>
class ESP32UartIrqAdapter
{
 public:
  /**
   * @brief 绑定非 owning owner 引用 / Bind a non-owning owner reference
   * @param owner adapter 可调用期间必须保持有效的 ESP UART 后端 / ESP UART backend that
   * must remain valid while the adapter is callable
   */
  explicit ESP32UartIrqAdapter(Owner& owner) : owner_(owner) {}

  /** @brief 取得 IRQ 域锁并屏蔽完整 IRQ 域 / Lock and mask the complete IRQ domain. */
  void LockAndMaskIrqDomain() noexcept
  {
    portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_);
    owner_.SetIrqDomainEnabledLocked(false);
  }

  /**
   * @brief 保持 IRQ 域屏蔽并释放其锁 / Leave the IRQ domain masked and release its lock
   */
  void UnlockIrqDomain() noexcept { portEXIT_CRITICAL_SAFE(&owner_.irq_domain_lock_); }

  /** @brief 在 IRQ 域已屏蔽时取得其锁 / Lock an already masked IRQ domain. */
  void LockIrqDomain() noexcept { portENTER_CRITICAL_SAFE(&owner_.irq_domain_lock_); }

  /**
   * @brief 恢复完整 IRQ 域并原子释放其锁 / Restore the complete IRQ domain and release
   * its lock atomically
   */
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

/**
 * @brief 单核或 FreeRTOS unicore 构建使用的 ESP direct policy / ESP direct policy for a
 * single-core or FreeRTOS unicore build
 * @tparam Owner ESP UART 后端类型 / ESP UART backend type
 */
template <typename Owner>
class ESP32UartExecutionPolicy<Owner, false>
{
 public:
  /**
   * @brief 构造 direct policy；无需 IRQ-domain adapter / Construct a direct policy
   * without an IRQ-domain adapter
   */
  explicit ESP32UartExecutionPolicy(Owner&) {}

  /**
   * @brief 合并事件并同步推进 direct owner / Merge events and synchronously advance the
   * direct owner
   * @tparam Handler owner 处理器类型 / Owner-handler type
   * @param events 待合并的事件位 / Event bits to merge
   * @param handler owner 取得执行权后调用的处理器 / Handler called by the acquired owner
   * @return 本次调用取得并推进 owner 时为 true / True when this call acquired and
   * advanced the owner
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  /**
   * @brief 读取 IRQ source 后通过 direct owner 处理快照 / Read the IRQ source, then
   * process its snapshot through the direct owner
   * @tparam Source IRQ source 函数类型 / IRQ-source function type
   * @tparam Handler owner 处理器类型 / Owner-handler type
   * @param source 读取并确认 IRQ 快照的函数 / Function that reads and acknowledges the
   * IRQ snapshot
   * @param handler 处理快照并推进 owner 的函数 / Function that processes the snapshot and
   * advances the owner
   * @return 本次调用取得并推进 owner 时为 true / True when this call acquired and
   * advanced the owner
   */
  template <typename Source, typename Handler>
  bool InvokeIrq(Source&& source, Handler&& handler) noexcept
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             std::forward<Handler>(handler));
  }

 private:
  UartDirectPolicy policy_{};
};

/**
 * @brief 多核 FreeRTOS 构建使用的 ESP raw-IRQ 串行策略 / ESP raw-IRQ serialization for
 * a multicore FreeRTOS build
 * @tparam Owner ESP UART 后端类型 / ESP UART backend type
 */
template <typename Owner>
class ESP32UartExecutionPolicy<Owner, true>
{
 public:
  /**
   * @brief 绑定 owner 的完整 IRQ 域 / Bind the owner's complete IRQ domain
   * @param owner ESP UART 后端 / ESP UART backend
   */
  explicit ESP32UartExecutionPolicy(Owner& owner) : adapter_(owner), policy_(adapter_) {}

  /**
   * @brief 屏蔽完整 IRQ 域后合并事件并推进 owner / Mask the complete IRQ domain, merge
   * events, and advance the owner
   * @tparam Handler owner 处理器类型 / Owner-handler type
   * @param events 待合并的事件位 / Event bits to merge
   * @param handler owner 取得执行权后调用的处理器 / Handler called by the acquired owner
   * @return 本次调用取得并推进 owner 时为 true / True when this call acquired and
   * advanced the owner
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    return policy_.Invoke(events, std::forward<Handler>(handler));
  }

  /**
   * @brief 在读取 IRQ source 前取得 raw-IRQ admission，再由 owner 处理快照 / Acquire
   * raw-IRQ admission before reading the IRQ source, then process the snapshot through
   * the owner
   * @tparam Source IRQ source 函数类型 / IRQ-source function type
   * @tparam Handler owner 处理器类型 / Owner-handler type
   * @param source admission 后读取并确认 IRQ 快照的函数 / Function that reads and
   * acknowledges the IRQ snapshot after admission
   * @param handler 处理快照并推进 owner 的函数 / Function that processes the snapshot and
   * advances the owner
   * @return 本次调用取得并推进 owner 时为 true / True when this call acquired and
   * advanced the owner
   */
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
