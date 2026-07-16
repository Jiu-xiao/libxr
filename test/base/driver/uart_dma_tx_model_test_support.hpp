#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#include "../../common/rw/rw_mode_test_common.hpp"
#include "latest_snapshot.hpp"
#include "model/uart_dma_tx_model.hpp"
#include "model/uart_hardware_gate.hpp"
#include "test.hpp"

namespace LibXRTest::UartDmaTx
{

inline constexpr size_t DMA_BLOCK_SIZE = 8U;
inline constexpr uint32_t WAIT_TIMEOUT_MS = 500U;

template <typename Predicate>
bool WaitUntil(Predicate&& predicate, uint32_t timeout_ms = WAIT_TIMEOUT_MS)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!predicate())
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

class FakeUartDmaBackend
{
 public:
  using HardwareOwnerContext = LibXR::UartHardwareGate::OwnerContext;

  struct Configuration
  {
    uint32_t value = 0U;
  };

  struct StartRecord
  {
    uint8_t data[DMA_BLOCK_SIZE]{};
    size_t size = 0U;
    int block = 0;
    uint32_t nested = 0U;
  };

  FakeUartDmaBackend()
      : port_(8U, DMA_BLOCK_SIZE),
        model_(*this, port_, LibXR::RawData(storage_, sizeof(storage_)))
  {
    port_ = WriteFun;
  }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto* backend = LibXR::ContainerOf(&port, &FakeUartDmaBackend::port_);
    ContextScope scope(in_isr);
    return backend->model_.Submit(in_isr);
  }

  LibXR::ErrorCode Write(const uint8_t* data, size_t size, LibXR::WriteOperation& op,
                         bool in_isr = false)
  {
    return port_(LibXR::ConstRawData(data, size), op, in_isr);
  }

  LibXR::WritePort& Port() { return port_; }

  void EnableHardwareGateForTest()
  {
    ASSERT(hardware_gate_enabled_.load(std::memory_order_acquire) == 0U);
    hardware_gate_enabled_.store(1U, std::memory_order_release);
  }

  [[nodiscard]] bool EnterIrqForTest()
  {
    ASSERT(HardwareGateEnabled());
    const bool entered = hardware_gate_.TryEnterIrq(irq_test_context_);
    if (entered)
    {
      irq_test_owner_.store(1U, std::memory_order_release);
    }
    return entered;
  }

  void CompleteInHeldIrq(bool in_isr = true)
  {
    ASSERT(HardwareGateEnabled());
    ASSERT(irq_test_owner_.load(std::memory_order_acquire) != 0U);
    ContextScope scope(in_isr);
    DispatchCompleteToModel(in_isr, &irq_test_context_);
  }

  void LeaveIrqForTest(bool in_isr = true)
  {
    DispatchActionsForTest(TakeIrqActionsForTest(), in_isr);
  }

  [[nodiscard]] LibXR::UartHardwareGate::PendingAction TakeIrqActionsForTest()
  {
    ASSERT(HardwareGateEnabled());
    ASSERT(irq_test_owner_.exchange(0U, std::memory_order_acq_rel) != 0U);
    return hardware_gate_.LeaveIrq(irq_test_context_);
  }

  void DispatchActionsForTest(LibXR::UartHardwareGate::PendingAction actions,
                              bool in_isr = true)
  {
    ContextScope scope(in_isr);
    DispatchHardwareActions(actions, in_isr);
  }

  [[nodiscard]] bool HardwareIrqDeferred() const
  {
    return IrqStatusLatched() || IrqDeferredBit();
  }

  [[nodiscard]] bool IrqStatusLatched() const
  {
    return deferred_irq_events_.load(std::memory_order_acquire) != 0U;
  }

  [[nodiscard]] bool IrqDeferredBit() const
  {
    return HardwareGateEnabled() && hardware_gate_.IrqDeferred();
  }

  [[nodiscard]] uint32_t ModelCompleteDispatchCount() const
  {
    return model_complete_dispatch_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t ModelErrorDispatchCount() const
  {
    return model_error_dispatch_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t DeferredRemaskCount() const
  {
    return deferred_remask_count_.load(std::memory_order_acquire);
  }

  void MarkDeferredCompleteForTest()
  {
    ASSERT(HardwareGateEnabled());
    LatchIrqStatus(IRQ_COMPLETE);
    hardware_gate_.MarkIrqDeferred();
  }

  void MarkIrqDeferredOnlyForTest()
  {
    ASSERT(HardwareGateEnabled());
    hardware_gate_.MarkIrqDeferred();
  }

  void DmaTcIntermediateDuringConfig(bool in_isr = true)
  {
    ASSERT(HardwareGateEnabled());
    ContextScope scope(in_isr);
    LatchIrqStatus(IRQ_COMPLETE);
    model_.ResumeConfig(in_isr);
  }

  void BlockNextDeferredPublishForTest()
  {
    deferred_publish_blocked_.store(0U, std::memory_order_release);
    release_deferred_publish_.store(0U, std::memory_order_release);
    block_deferred_publish_.store(1U, std::memory_order_release);
  }

  [[nodiscard]] bool WaitUntilDeferredPublishBlocked()
  {
    return WaitUntil(
        [&] { return deferred_publish_blocked_.load(std::memory_order_acquire) != 0U; });
  }

  void ReleaseDeferredPublishForTest()
  {
    release_deferred_publish_.store(1U, std::memory_order_release);
  }

  void Complete(bool in_isr = false)
  {
    ContextScope scope(in_isr);
    if (!HardwareGateEnabled())
    {
      DispatchCompleteToModel(in_isr);
      return;
    }

    HardwareOwnerContext hardware_context;
    // The peripheral status is already latched when the vector reaches this
    // adapter. Publish that fact before trying to acquire the shared owner; a
    // CONFIG owner may clear it while this IRQ is waiting to mark its deferred
    // handoff.
    LatchIrqStatus(IRQ_COMPLETE);
    model_.ResumeConfig(in_isr);
    if (!hardware_gate_.TryEnterIrq(hardware_context))
    {
      MaskNormalIrqDomain();
      MaybeBlockDeferredPublish();
      hardware_gate_.MarkIrqDeferred();
      DispatchHardwareActions(LibXR::UartHardwareGate::PendingAction::NONE, in_isr);
      return;
    }

    if (hardware_gate_.IrqDeferred())
    {
      DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
      return;
    }

    DispatchLatchedIrq(in_isr, &hardware_context);
    DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
  }

  void Error(bool in_isr = false)
  {
    ContextScope scope(in_isr);
    if (!HardwareGateEnabled())
    {
      DispatchErrorToModel(in_isr);
      return;
    }

    HardwareOwnerContext hardware_context;
    // See Complete(): ERROR is a hardware status fact, not a notification that
    // may be published only after owner acquisition.
    LatchIrqStatus(IRQ_ERROR);
    model_.ResumeConfig(in_isr);
    if (!hardware_gate_.TryEnterIrq(hardware_context))
    {
      MaskNormalIrqDomain();
      MaybeBlockDeferredPublish();
      hardware_gate_.MarkIrqDeferred();
      DispatchHardwareActions(LibXR::UartHardwareGate::PendingAction::NONE, in_isr);
      return;
    }

    if (hardware_gate_.IrqDeferred())
    {
      DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
      return;
    }

    DispatchLatchedIrq(in_isr, &hardware_context);
    DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
  }

  void Configure(uint32_t value, bool in_isr = false)
  {
    requested_config_.Store(Configuration{value});
    ContextScope scope(in_isr);
    model_.RequestConfig(in_isr);
  }

  void RetryConfig(bool in_isr = false)
  {
    ContextScope scope(in_isr);
    model_.RequestConfig(in_isr);
  }

  void ResumeConfig(bool in_isr = false)
  {
    ContextScope scope(in_isr);
    model_.ResumeConfig(in_isr);
  }

  [[nodiscard]] bool IsBusy() const { return model_.IsBusy(); }
  [[nodiscard]] size_t BufferSize() const { return model_.BufferSize(); }

  [[nodiscard]] const StartRecord& StartAt(size_t index) const
  {
    ASSERT(index < StartCount());
    return starts_[index];
  }

  [[nodiscard]] size_t StartCount() const
  {
    return start_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] size_t QueuedInfoCount() const { return port_.queue_info_->Size(); }
  [[nodiscard]] size_t QueuedDataSize() const { return port_.queue_data_->Size(); }

  void SetStartAllowed(bool allowed)
  {
    allow_start_.store(allowed ? 1U : 0U, std::memory_order_release);
  }

  void RetryNextStart() { retry_next_start_.store(1U, std::memory_order_release); }

  void ResumeStart(bool in_isr = false)
  {
    ContextScope scope(in_isr);
    model_.ResumeStart(in_isr);
  }

  void SetConfigApplyAllowed(bool allowed)
  {
    allow_config_apply_.store(allowed ? 1U : 0U, std::memory_order_release);
  }

  void ResumeNextConfigSynchronously()
  {
    resume_config_during_apply_.store(1U, std::memory_order_release);
  }

  void CompleteNextStartWithSpuriousConfig()
  {
    complete_with_spurious_config_at_.store(static_cast<uint32_t>(StartCount()),
                                            std::memory_order_release);
  }

  void BlockNextStart()
  {
    start_blocked_.store(0U, std::memory_order_release);
    release_start_.store(0U, std::memory_order_release);
    block_start_at_.store(static_cast<uint32_t>(StartCount()), std::memory_order_release);
  }

  bool WaitUntilStartBlocked()
  {
    return WaitUntil([&]
                     { return start_blocked_.load(std::memory_order_acquire) != 0U; });
  }

  void ReleaseBlockedStart() { release_start_.store(1U, std::memory_order_release); }

  void BlockNextConfig()
  {
    config_blocked_.store(0U, std::memory_order_release);
    release_config_.store(0U, std::memory_order_release);
    block_config_at_.store(ConfigApplyCount(), std::memory_order_release);
  }

  bool WaitUntilConfigBlocked()
  {
    return WaitUntil([&]
                     { return config_blocked_.load(std::memory_order_acquire) != 0U; });
  }

  void ReleaseBlockedConfig() { release_config_.store(1U, std::memory_order_release); }

  [[nodiscard]] uint32_t ConfigApplyCount() const
  {
    return config_apply_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t AppliedConfig() const
  {
    return applied_config_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t ConfigInIsr() const
  {
    return config_in_isr_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t RxStopCount() const
  {
    return rx_stop_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] uint32_t RxRestartCount() const
  {
    return rx_restart_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool RxRunning() const
  {
    return rx_running_.load(std::memory_order_acquire) != 0U;
  }

 private:
  friend class LibXR::UartDmaTxModel<FakeUartDmaBackend>;

  static constexpr uint32_t IRQ_COMPLETE = 1U << 0U;
  static constexpr uint32_t IRQ_ERROR = 1U << 1U;

  class ContextScope
  {
   public:
    explicit ContextScope(bool in_isr) : previous_(CurrentContextInIsr())
    {
      CurrentContextInIsr() = in_isr;
    }

    ~ContextScope() { CurrentContextInIsr() = previous_; }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

   private:
    bool previous_;
  };

  static bool& CurrentContextInIsr()
  {
    static thread_local bool in_isr = false;
    return in_isr;
  }

  [[nodiscard]] bool HardwareGateEnabled() const
  {
    return hardware_gate_enabled_.load(std::memory_order_acquire) != 0U;
  }

  [[nodiscard]] static constexpr bool HasAction(
      LibXR::UartHardwareGate::PendingAction actions,
      LibXR::UartHardwareGate::PendingAction action)
  {
    return LibXR::UartHardwareGate::HasAction(actions, action);
  }

  void MaybeBlockDeferredPublish()
  {
    if (block_deferred_publish_.exchange(0U, std::memory_order_acq_rel) == 0U)
    {
      return;
    }

    deferred_publish_blocked_.store(1U, std::memory_order_release);
    const bool released = WaitUntil(
        [&] { return release_deferred_publish_.load(std::memory_order_acquire) != 0U; });
    ASSERT(released);
  }

  void DispatchCompleteToModel(bool in_isr,
                               HardwareOwnerContext* hardware_context = nullptr)
  {
    model_complete_dispatch_count_.fetch_add(1U, std::memory_order_acq_rel);
    if (hardware_context == nullptr)
    {
      model_.OnTransferDone(in_isr);
    }
    else
    {
      model_.OnTransferDone(in_isr, *hardware_context);
    }
  }

  void DispatchErrorToModel(bool in_isr, HardwareOwnerContext* hardware_context = nullptr)
  {
    model_error_dispatch_count_.fetch_add(1U, std::memory_order_acq_rel);
    if (hardware_context == nullptr)
    {
      model_.OnTransferError(in_isr);
    }
    else
    {
      model_.OnTransferError(in_isr, *hardware_context);
    }
  }

  void LatchIrqStatus(uint32_t event)
  {
    deferred_irq_events_.fetch_or(event, std::memory_order_release);
  }

  void DispatchLatchedIrq(bool in_isr, HardwareOwnerContext* hardware_context)
  {
    const uint32_t events = deferred_irq_events_.exchange(0U, std::memory_order_acq_rel);
    if ((events & IRQ_ERROR) != 0U)
    {
      // ERROR has priority over a co-latched COMPLETE. The completion belongs
      // to the same hardware generation and must not release a newly promoted
      // active request after recovery.
      DispatchErrorToModel(in_isr, hardware_context);
    }
    else if ((events & IRQ_COMPLETE) != 0U)
    {
      DispatchCompleteToModel(in_isr, hardware_context);
    }
  }

  void MaskNormalIrqDomain()
  {
    (void)irq_domain_enabled_.exchange(0U, std::memory_order_seq_cst);
  }

  void RestoreNormalIrqDomain()
  {
    (void)irq_domain_enabled_.exchange(1U, std::memory_order_seq_cst);
  }

  bool ProcessDeferredIrq(bool in_isr)
  {
    HardwareOwnerContext hardware_context;
    if (!hardware_gate_.TryEnterDeferredIrq(hardware_context))
    {
      return false;
    }

    // A prior owner may have restored the authoritative enable mask while a losing
    // vector was between mask and MarkIrqDeferred(). Re-mask after the durable claim so
    // the full-domain scan never relies on the publisher's earlier mask still winning.
    MaskNormalIrqDomain();
    deferred_remask_count_.fetch_add(1U, std::memory_order_acq_rel);
    const uint32_t events = deferred_irq_events_.exchange(0U, std::memory_order_acq_rel);
    ContextScope scope(in_isr);
    if ((events & IRQ_ERROR) != 0U)
    {
      DispatchErrorToModel(in_isr, &hardware_context);
    }
    else if ((events & IRQ_COMPLETE) != 0U)
    {
      DispatchCompleteToModel(in_isr, &hardware_context);
    }
    RestoreNormalIrqDomain();
    DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
    return true;
  }

  void DispatchHardwareActions(LibXR::UartHardwareGate::PendingAction actions,
                               bool in_isr)
  {
    if (!HardwareGateEnabled())
    {
      return;
    }

    // CONFIG closes the old hardware generation first. A deferred terminal is then
    // retried at the same boundary, and only finally may a TX start be resumed.
    if (HasAction(actions, LibXR::UartHardwareGate::PendingAction::CONFIG))
    {
      ResumeConfig(in_isr);
    }

    const bool deferred =
        HasAction(actions, LibXR::UartHardwareGate::PendingAction::IRQ_DEFERRED) ||
        hardware_gate_.IrqDeferred();
    if (deferred && !ProcessDeferredIrq(in_isr))
    {
      return;
    }

    if (HasAction(actions, LibXR::UartHardwareGate::PendingAction::TX_START))
    {
      ResumeStart(in_isr);
    }
  }

  LibXR::UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block,
                                         HardwareOwnerContext* hardware_context)
  {
    if (retry_next_start_.exchange(0U, std::memory_order_acq_rel) != 0U)
    {
      // This test-only injection has no hardware-gate carrier. Gate-enabled RETRY must
      // come only from a failed admission, which publishes TX_START_PENDING itself.
      ASSERT(!HardwareGateEnabled());
      return LibXR::UartDmaTxStartResult::RETRY;
    }

    const bool gate_enabled = HardwareGateEnabled();
    const bool nested_start = gate_enabled && (hardware_context != nullptr);
    if (nested_start && !hardware_gate_.TryEnterNestedTxStart(*hardware_context))
    {
      return LibXR::UartDmaTxStartResult::RETRY;
    }
    if (gate_enabled && !nested_start && !hardware_gate_.TryEnterTxStart())
    {
      return LibXR::UartDmaTxStartResult::RETRY;
    }

    const auto finish_start = [&](LibXR::UartDmaTxStartResult result)
    {
      if (gate_enabled)
      {
        if (nested_start)
        {
          hardware_gate_.LeaveNestedTxStart(*hardware_context);
        }
        else
        {
          DispatchHardwareActions(hardware_gate_.LeaveTxStart(), CurrentContextInIsr());
        }
      }
      return result;
    };

    const uint32_t start_index = static_cast<uint32_t>(StartCount());
    if (block_start_at_.load(std::memory_order_acquire) == start_index)
    {
      start_blocked_.store(1U, std::memory_order_release);
      const bool released =
          WaitUntil([&] { return release_start_.load(std::memory_order_acquire) != 0U; });
      ASSERT(released);
      block_start_at_.store(UINT32_MAX, std::memory_order_release);
    }

    if (allow_start_.load(std::memory_order_acquire) == 0U)
    {
      return finish_start(LibXR::UartDmaTxStartResult::FAILED);
    }

    ASSERT(start_index < (sizeof(starts_) / sizeof(starts_[0])));
    ASSERT(size <= sizeof(starts_[start_index].data));
    StartRecord& record = starts_[start_index];
    std::memcpy(record.data, data, size);
    record.size = size;
    record.block = block;
    record.nested = nested_start ? 1U : 0U;
    start_count_.store(start_index + 1U, std::memory_order_release);
    if (complete_with_spurious_config_at_.exchange(
            UINT32_MAX, std::memory_order_acq_rel) == start_index)
    {
      model_.ResumeConfig(true);
      DispatchCompleteToModel(true, hardware_context);
    }
    return finish_start(LibXR::UartDmaTxStartResult::STARTED);
  }

  void OnConfigRequested()
  {
    if (HardwareGateEnabled())
    {
      hardware_gate_.RequestConfig();
    }
    else
    {
      config_pending_.store(1U, std::memory_order_release);
    }
  }

  [[nodiscard]] bool ConfigRequested() const
  {
    return HardwareGateEnabled() ? hardware_gate_.ConfigRequested()
                                 : config_pending_.load(std::memory_order_acquire) != 0U;
  }

  bool ApplyPendingConfig(bool in_isr)
  {
    if (resume_config_during_apply_.exchange(0U, std::memory_order_acq_rel) != 0U)
    {
      model_.ResumeConfig(in_isr);
      return false;
    }

    if (allow_config_apply_.load(std::memory_order_acquire) == 0U)
    {
      return false;
    }

    if (HardwareGateEnabled() && !hardware_gate_.ConfigActive() &&
        !hardware_gate_.TryEnterConfig())
    {
      return false;
    }

    if (HardwareGateEnabled())
    {
      MaskNormalIrqDomain();
      StopRxForConfig();
      hardware_gate_.ConsumePendingConfig();
      deferred_irq_events_.exchange(0U, std::memory_order_acq_rel);
    }
    else
    {
      ASSERT(config_pending_.exchange(0U, std::memory_order_acq_rel) != 0U);
    }

    if (!ApplyConfigPayload(in_isr))
    {
      return false;
    }
    if (HardwareGateEnabled())
    {
      RestartRxAfterConfig();
    }
    return true;
  }

  bool ApplyConfigPayload(bool in_isr)
  {
    const uint32_t apply_index = ConfigApplyCount();
    if (block_config_at_.load(std::memory_order_acquire) == apply_index)
    {
      config_blocked_.store(1U, std::memory_order_release);
      const bool released = WaitUntil(
          [&] { return release_config_.load(std::memory_order_acquire) != 0U; });
      ASSERT(released);
      block_config_at_.store(UINT32_MAX, std::memory_order_release);
    }

    Configuration config{};
    (void)requested_config_.LoadLatest(config);
    applied_config_.store(config.value, std::memory_order_release);
    config_in_isr_.store(in_isr ? 1U : 0U, std::memory_order_release);
    config_apply_count_.store(apply_index + 1U, std::memory_order_release);
    return true;
  }

  void StopRxForConfig()
  {
    if (rx_running_.exchange(0U, std::memory_order_acq_rel) != 0U)
    {
      rx_stop_count_.fetch_add(1U, std::memory_order_acq_rel);
    }
  }

  void RestartRxAfterConfig()
  {
    if (rx_running_.exchange(1U, std::memory_order_acq_rel) == 0U)
    {
      rx_restart_count_.fetch_add(1U, std::memory_order_acq_rel);
    }
  }

  bool OnConfigApplied(bool in_isr)
  {
    if (HardwareGateEnabled())
    {
      RestoreNormalIrqDomain();
      DispatchHardwareActions(hardware_gate_.LeaveConfig(), in_isr);
    }
    return true;
  }

  alignas(size_t) uint8_t storage_[DMA_BLOCK_SIZE * 2U]{};
  LibXR::WritePort port_;
  LibXR::LatestSnapshot<Configuration> requested_config_{Configuration{}};
  LibXR::UartDmaTxModel<FakeUartDmaBackend> model_;
  StartRecord starts_[32]{};
  std::atomic<uint32_t> start_count_{0U};
  std::atomic<uint32_t> allow_start_{1U};
  std::atomic<uint32_t> retry_next_start_{0U};
  std::atomic<uint32_t> block_start_at_{UINT32_MAX};
  std::atomic<uint32_t> start_blocked_{0U};
  std::atomic<uint32_t> release_start_{0U};
  std::atomic<uint32_t> config_pending_{0U};
  std::atomic<uint32_t> allow_config_apply_{1U};
  std::atomic<uint32_t> resume_config_during_apply_{0U};
  std::atomic<uint32_t> complete_with_spurious_config_at_{UINT32_MAX};
  std::atomic<uint32_t> config_apply_count_{0U};
  std::atomic<uint32_t> applied_config_{0U};
  std::atomic<uint32_t> config_in_isr_{UINT32_MAX};
  std::atomic<uint32_t> block_config_at_{UINT32_MAX};
  std::atomic<uint32_t> config_blocked_{0U};
  std::atomic<uint32_t> release_config_{0U};
  LibXR::UartHardwareGate hardware_gate_{};
  std::atomic<uint32_t> hardware_gate_enabled_{0U};
  std::atomic<uint32_t> irq_test_owner_{0U};
  // Test-only scheduler seam: production contexts remain local to one synchronous IRQ
  // call stack. The fixture retains one only to split deterministic enter/dispatch/leave
  // steps across test helper calls.
  HardwareOwnerContext irq_test_context_{};
  std::atomic<uint32_t> deferred_irq_events_{0U};
  std::atomic<uint32_t> block_deferred_publish_{0U};
  std::atomic<uint32_t> deferred_publish_blocked_{0U};
  std::atomic<uint32_t> release_deferred_publish_{0U};
  std::atomic<uint32_t> model_complete_dispatch_count_{0U};
  std::atomic<uint32_t> model_error_dispatch_count_{0U};
  std::atomic<uint32_t> irq_domain_enabled_{1U};
  std::atomic<uint32_t> deferred_remask_count_{0U};
  std::atomic<uint32_t> rx_running_{1U};
  std::atomic<uint32_t> rx_stop_count_{0U};
  std::atomic<uint32_t> rx_restart_count_{0U};
};

inline void AssertStart(const FakeUartDmaBackend& backend, size_t index,
                        const uint8_t* data, size_t size, int block)
{
  const auto& record = backend.StartAt(index);
  ASSERT(record.size == size);
  ASSERT(record.block == block);
  ASSERT(std::memcmp(record.data, data, size) == 0);
}

struct CallbackProbe
{
  std::atomic<uint32_t> count{0U};
  std::atomic<uint32_t> in_isr{UINT32_MAX};
  std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
  LibXR::WriteOperation::Callback callback =
      LibXR::WriteOperation::Callback::Create(OnComplete, this);

  static void OnComplete(bool in_isr, CallbackProbe* self, LibXR::ErrorCode result)
  {
    self->in_isr.store(in_isr ? 1U : 0U, std::memory_order_release);
    self->result.store(static_cast<uint32_t>(result), std::memory_order_release);
    self->count.fetch_add(1U, std::memory_order_acq_rel);
  }

  bool WaitForCount(uint32_t expected)
  {
    return WaitUntil([&] { return count.load(std::memory_order_acquire) == expected; });
  }
};

void RunStateTests();
void RunBoundaryTests();
void RunOperationTests();
void RunConcurrencyTests();

}  // namespace LibXRTest::UartDmaTx
