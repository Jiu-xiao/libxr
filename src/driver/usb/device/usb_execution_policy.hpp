#pragma once

#include <cstdint>
#include <utility>

#include "libxr_assert.hpp"
#include "serialized_service.hpp"

namespace LibXR::USB
{

/**
 * @brief 每个 USB device 的唯一执行 owner / Sole execution owner for one USB device
 *
 * 普通 work 发布者在短 admission guard 内屏蔽该 device 的完整 IRQ 域，再尝试取得
 * owner。raw IRQ 也通过同一 guard，在首次读取或确认 USB 状态前取得 owner。guard 只
 * 覆盖 mask、owner CAS 和 restore；handler 执行期间不持跨核锁。 / Ordinary work
 * publishers mask the complete IRQ domain under a short admission guard before trying
 * to acquire the owner. Raw IRQs use the same guard and acquire the owner before their
 * first USB status read or acknowledgement. The guard covers only mask, owner CAS, and
 * restore; no cross-core lock is held while a handler runs.
 */
class USBExecutionPolicy
{
 public:
  /** @brief 后端 IRQ 域原语 / Backend IRQ-domain primitives. */
  struct InterruptDomain
  {
    using Lock = void (*)(void* context) noexcept;
    using Mask = uintptr_t (*)(void* context) noexcept;
    using Restore = void (*)(void* context, uintptr_t saved_state) noexcept;

    void* context = nullptr;
    Lock lock = nullptr;
    Lock unlock = nullptr;
    Mask mask = nullptr;
    Restore restore = nullptr;
  };

  using WorkHandler = void (*)(void* context, bool in_isr) noexcept;

  USBExecutionPolicy() = default;
  USBExecutionPolicy(const USBExecutionPolicy&) = delete;
  USBExecutionPolicy& operator=(const USBExecutionPolicy&) = delete;

  /**
   * @brief 绑定构造期固定的 class-work handler / Bind the construction-time fixed
   * class-work handler
   */
  void SetWorkHandler(void* context, WorkHandler handler) noexcept
  {
    ASSERT(handler != nullptr);
    ASSERT(work_handler_ == nullptr ||
           (work_handler_ == handler && work_context_ == context));
    work_context_ = context;
    work_handler_ = handler;
  }

  /**
   * @brief 绑定后端的完整 IRQ 域 / Bind the backend's complete IRQ domain
   *
   * lock/unlock 必须同时提供或同时为空。单核后端可省略二者。restore 通常恢复入口 mask；
   * 若 owner 内 Start/Stop 改变了 lifecycle intent，则必须恢复当前 desired mask，不能用
   * saved_state 重新打开已停止的 IRQ。 / lock/unlock must either both be present or
   * both be null. Single-core backends may omit them. Restore normally reinstates the
   * entry mask; when Start/Stop changes lifecycle intent under the owner, it must instead
   * apply the current desired mask and must not reopen a stopped IRQ from saved_state.
   */
  void SetInterruptDomain(InterruptDomain domain) noexcept
  {
    ASSERT(domain.mask != nullptr);
    ASSERT(domain.restore != nullptr);
    ASSERT((domain.lock == nullptr) == (domain.unlock == nullptr));
    ASSERT(!irq_guard_.configured_);
    irq_guard_.domain_ = domain;
    irq_guard_.configured_ = true;
  }

  /**
   * @brief 发布 class work 并主动提供 carrier / Publish class work and provide an
   * immediate carrier
   * @param in_isr 当前执行上下文是否为 ISR / Whether the current context is an ISR
   */
  void NotifyWork(bool in_isr) noexcept
  {
    ASSERT(work_handler_ != nullptr);
    auto handler = [this, in_isr](uint32_t) noexcept { DispatchWork(in_isr); };
    if (irq_guard_.configured_)
    {
      (void)service_.InvokeGuarded(WORK_EVENT, irq_guard_, handler);
    }
    else
    {
      (void)service_.Invoke(WORK_EVENT, handler);
    }
  }

  /**
   * @brief 在首次 USB 状态访问前取得 raw-IRQ owner / Acquire the raw-IRQ owner before
   * the first USB status access
   * @tparam Source 无参数 raw IRQ handler / Nullary raw IRQ handler
   * @param source 完整读取、确认并分发本批 IRQ 的 handler / Handler that reads,
   * acknowledges, and dispatches the complete IRQ batch
   * @param in_isr 当前执行上下文是否为 ISR / Whether the current context is an ISR
   * @return 本调用执行了 source 时为 true / True when this call executed source
   */
  template <typename Source>
  bool RunIrq(Source&& source, bool in_isr = true) noexcept
  {
    ASSERT(work_handler_ != nullptr);
    auto owned_source = [&source]() noexcept -> uint32_t
    {
      Source& source_ref = source;
      source_ref();
      return 0U;
    };
    auto handler = [this, in_isr](uint32_t) noexcept { DispatchWork(in_isr); };

    if (irq_guard_.configured_)
    {
      return service_.ClaimAndInvokeGuarded(irq_guard_, owned_source, handler);
    }
    return service_.ClaimAndInvoke(owned_source, handler);
  }

 private:
  class IrqGuard
  {
   public:
    void LockAndMaskIrqDomain() noexcept
    {
      Lock();
      if (!masked_)
      {
        saved_state_ = domain_.mask(domain_.context);
        masked_ = true;
      }
    }

    void UnlockIrqDomain() noexcept { Unlock(); }

    void LockIrqDomain() noexcept { Lock(); }

    void RestoreAndUnlockIrqDomain() noexcept
    {
      ASSERT(masked_);
      const uintptr_t saved_state = saved_state_;
      masked_ = false;
      domain_.restore(domain_.context, saved_state);
      Unlock();
    }

   private:
    void Lock() noexcept
    {
      if (domain_.lock != nullptr)
      {
        domain_.lock(domain_.context);
      }
    }

    void Unlock() noexcept
    {
      if (domain_.unlock != nullptr)
      {
        domain_.unlock(domain_.context);
      }
    }

    friend class USBExecutionPolicy;

    InterruptDomain domain_{};
    uintptr_t saved_state_ = 0U;
    bool configured_ = false;
    bool masked_ = false;
  };

  void DispatchWork(bool in_isr) noexcept
  {
    ASSERT(work_handler_ != nullptr);
    work_handler_(work_context_, in_isr);
  }

  static constexpr uint32_t WORK_EVENT = 1U;

  SerializedService service_{};
  IrqGuard irq_guard_{};
  void* work_context_ = nullptr;
  WorkHandler work_handler_ = nullptr;
};

}  // namespace LibXR::USB
