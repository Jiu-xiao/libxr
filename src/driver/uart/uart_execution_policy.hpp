#pragma once

#include <cstdint>
#include <utility>

#include "driver/common/serialized_service.hpp"

namespace LibXR
{

/**
 * @brief 不做 IRQ 域 admission 的逐 UART 串行执行策略 / Per-UART serialized execution
 * without IRQ-domain admission
 *
 * Direct 表示 vendor IRQ/HAL 代码保持不变；WRITE、COMPLETE、ERROR 和 CONFIG 仍共享
 * 本策略的唯一 `SerializedService` owner。 / Direct leaves vendor IRQ/HAL code
 * unchanged. WRITE, COMPLETE, ERROR, and CONFIG still share one owner.
 */
class UartDirectPolicy
{
 public:
  /**
   * @brief 发布事件并在当前上下文推进唯一 owner / Publish events and advance the
   * single owner in the current context
   * @tparam Handler 签名为 `uint32_t(uint32_t)` 的 service handler / Service handler
   * with signature `uint32_t(uint32_t)`
   * @param events 待合并的 UART 事件掩码 / UART event mask to coalesce
   * @param handler 消费快照并返回 continuation 事件的 handler / Handler that consumes
   * a snapshot and returns continuation events
   * @return 本调用取得并释放 owner 时为 true，否则为 false / True when this call
   * acquired and released the owner; false otherwise
   */
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
   * @brief 静止、读取并确认单核 IRQ 源后发布其事件 / Quiesce, read, and acknowledge a
   * single-core IRQ source before publishing its events
   *
   * Direct 后端中 IRQ 与普通调用者不会在不同核心并行，因此不需要 raw-IRQ owner
   * admission。仍可立即重触发的条件型事件源必须由 `source` 在发布前屏蔽或变成 one-shot，
   * 若仍有工作再由 `handler` 恢复。仅确认状态不足以避免重复 IRQ 阻止被抢占 owner 恢复。
   * / Direct backends need no raw-IRQ owner admission because IRQ and normal callers
   * cannot run on different cores. A condition-triggered source that can immediately
   * reassert must be masked or made one-shot by `source`, then re-armed by `handler`.
   * Acknowledgement alone cannot prevent repeated IRQ entry from starving a preempted
   * owner.
   *
   * @tparam Source 静止并读取 IRQ 源、返回事件掩码的可调用对象 / Callable that
   * quiesces and reads the IRQ source, returning an event mask
   * @tparam Handler service handler 类型 / Service handler type
   * @param source IRQ 源读取和确认操作 / IRQ-source read and acknowledge operation
   * @param handler 消费事件并返回 continuation 的 handler / Handler that consumes
   * events and returns a continuation
   * @return 本调用推进 owner 时为 true，否则为 false / True when this call advanced
   * the owner; false otherwise
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

  /** @brief 构造一个空闲的 direct policy / Construct an idle direct policy. */
  UartDirectPolicy() = default;
  UartDirectPolicy(const UartDirectPolicy&) = delete;
  UartDirectPolicy& operator=(const UartDirectPolicy&) = delete;

 private:
  SerializedService service_{};
};

/**
 * @brief 带 SMP IRQ 域 admission 的逐 UART 串行执行策略 / Per-UART serialized
 * execution with SMP IRQ-domain admission
 *
 * 普通调用者将完整 IRQ 域屏蔽与 owner admission 串行化。LibXR 或应用持有的 raw ISR
 * 使用 `InvokeIrq()`，在读或确认受保护状态前取得同一 owner。无新事件的释放 CAS 与完整
 * IRQ 域恢复使用同一短 guard，避免旧恢复在更新的屏蔽与 owner claim 之间重开某个源。
 * / Normal callers serialize the complete IRQ-domain mask with owner admission. A raw
 * ISR uses `InvokeIrq()` to acquire the same owner before touching protected status.
 * Release CAS and complete-domain restore share one short guard so a stale restore cannot
 * reopen a source between a newer mask and claim.
 *
 * 本策略必须在 SDK/HAL 首次读或确认 IRQ 源之前取得控制。若只从 SDK 已处理事件源后的
 * callback 进入，则无法提供硬件串行化，除非 SDK 已为其 IRQ handler 与 UART/DMA API
 * 提供等价的跨核串行化。 / This policy requires control before the SDK/HAL first reads
 * or acknowledges the IRQ source. Entering only from a post-handler callback is too late
 * unless the SDK already provides equivalent cross-core serialization.
 *
 * @tparam Adapter 提供 IRQ 域锁、屏蔽、恢复和解锁操作的后端 / Backend providing the
 * IRQ-domain lock, mask, restore, and unlock operations
 */
template <typename Adapter>
class UartIrqSerializedPolicy
{
 public:
  /**
   * @brief 绑定后端 IRQ 域 adapter / Bind the backend IRQ-domain adapter
   * @param adapter 生命周期必须覆盖本策略的后端 adapter / Backend adapter that must
   * outlive this policy
   */
  explicit UartIrqSerializedPolicy(Adapter& adapter) : adapter_(adapter) {}

  /**
   * @brief 屏蔽 IRQ 域、发布事件并尝试取得 owner / Mask the IRQ domain, publish events,
   * and try to acquire the owner
   * @tparam Handler service handler 类型 / Service handler type
   * @param events 待合并的 UART 事件掩码 / UART event mask to coalesce
   * @param handler 消费快照并返回 continuation 的 handler / Handler that consumes a
   * snapshot and returns continuation events
   * @return 本调用取得并释放 owner 时为 true，否则为 false / True when this call
   * acquired and released the owner; false otherwise
   */
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
   * @brief 在首次访问受保护状态前 admit raw IRQ 源 / Admit a raw IRQ source before its
   * first protected status access
   *
   * `source` 必须包含首次受保护状态读取或确认。在 SDK/HAL IRQ handler 已触碰该状态后
   * 才调用本方法，无法保护 handler 免受另一核心并发硬件操作。 / `source` must contain
   * the first protected status read or acknowledgement. Calling this only after an
   * SDK/HAL handler has touched the status is too late to serialize that handler.
   *
   * @tparam Source 签名为 `uint32_t()`、读取或确认受保护事件源并返回 service 事件的
   * 可调用对象 / Callable with signature `uint32_t()` that reads or acknowledges the
   * protected source and returns service events
   * @tparam Handler 签名为 `uint32_t(uint32_t)` 的 service handler / Service handler
   * with signature `uint32_t(uint32_t)`
   * @param source 首次受保护状态访问 / First protected status access
   * @param handler 消费事件并返回 continuation 的 handler / Handler that consumes
   * events and returns a continuation
   * @return 本调用取得并释放 owner 时为 true，否则为 false / True when this call
   * acquired and released the owner; false otherwise
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
