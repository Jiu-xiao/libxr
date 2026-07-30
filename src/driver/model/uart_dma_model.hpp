#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "double_buffer_storage.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"
#include "uart_rx_config_gate.hpp"

namespace LibXR
{

/**
 * @brief Result of one backend DMA-start attempt.
 *
 * `STARTED` transfers ownership of the active buffer to hardware. `FAILED` guarantees
 * that no hardware transfer is live and no terminal callback from this attempt can be
 * published later; the model may immediately reuse the block and try the next record.
 */
enum class UartDmaTxStartResult : uint32_t
{
  STARTED = 0U,
  FAILED = 1U,
};

/** Progress of one non-blocking control hook. */
enum class UartDmaControlProgress : uint8_t
{
  COMPLETED = 0U,
  PENDING = 1U,
};

/** Authoritative terminal classification for the stopped TX generation. */
enum class UartOldTxTerminal : uint8_t
{
  NONE = 0U,
  COMPLETE = 1U,
  ERROR = 2U,
};

/**
 * @brief Result of one backend CONFIG or runtime-recovery advancement.
 *
 * `PENDING` carries no terminal classification and must arrange a future STOP_DONE,
 * COMPLETE, CONTROL_READY, or equivalent service carrier. `COMPLETED` is the hardware
 * quiescence linearization point: the classification is final, destructive cleanup has
 * completed, and no old terminal callback may be published later. COMPLETE is consumed
 * by the model in the following hardware-quiescent service snapshot; NONE and ERROR keep
 * the retained active payload for restart.
 */
struct UartDmaControlResult
{
  UartDmaControlProgress progress;
  UartOldTxTerminal old_tx_terminal;

  [[nodiscard]] static constexpr UartDmaControlResult Pending()
  {
    return {UartDmaControlProgress::PENDING, UartOldTxTerminal::NONE};
  }

  [[nodiscard]] static constexpr UartDmaControlResult Completed(
      UartOldTxTerminal terminal = UartOldTxTerminal::NONE)
  {
    return {UartDmaControlProgress::COMPLETED, terminal};
  }

  [[nodiscard]] constexpr bool IsCompleted() const
  {
    return progress == UartDmaControlProgress::COMPLETED;
  }
};

/** Coalescible facts consumed by one UART serialized service. */
enum class UartDmaEvent : uint32_t
{
  WRITE = 1U << 0U,
  COMPLETE = 1U << 1U,
  ERROR = 1U << 2U,
  CONFIG = 1U << 3U,
  STOP_DONE = 1U << 4U,
  CONTROL_READY = 1U << 5U,
};

/**
 * @brief Serialized UART DMA data-path and configuration model.
 *
 * The model owns one execution policy, CONFIG payload publication, RX/control admission,
 * one-active/one-pending TX state, and all lifecycle event routing. WRITE, COMPLETE,
 * ERROR, CONFIG, and asynchronous stop completion enter the same `SerializedService`.
 * Only its owner mutates TX state, dequeues WritePort, starts DMA, performs
 * recovery/configuration, or completes records.
 *
 * RX byte delivery stays outside the service as one direct SPSC producer. A standalone
 * RX callback enters through `ProcessRx()` after acknowledging IRQ status but before
 * reading DMA position/descriptors or moving bytes. A combined IRQ scanner uses
 * `ProcessRxInIrqSource()` so its control wakeup is published together with every old
 * COMPLETE/ERROR fact returned by that scan. CONFIG may discard an unadmitted fragment,
 * while a fragment that acquired the gate finishes before control advances.
 *
 * The pending payload is copied out of the public byte queue while its metadata remains
 * at the metadata-queue head. Promotion publishes active state before `StartDmaTx()`.
 * The model passes the current `in_isr` fact into the hook so backend hot paths do not
 * need to rediscover it. A terminal callback raised before that function returns only
 * merges another service event and is therefore processed after the owner commits
 * STARTED or FAILED.
 * If that owner observes CONFIG, its stack-local synchronous submission shortcut is
 * abandoned; any preserved public record completes through its durable Operation.
 *
 * `StartDmaTx() == FAILED` is record-local and never requests CONFIG. Runtime ERROR asks
 * the backend to recover the data path, preserves active and pending software records,
 * restarts the active DMA after quiescence when one exists, and resumes queued work.
 * CONFIG validates, reserves, stores, and publishes one complete payload through this
 * model. After quiescence it preserves active and pending records, preserves every
 * record still in the public WritePort queue, restarts active under the new
 * configuration when one exists, and republishes WRITE.
 *
 * `Backend::ValidateConfig()` runs before CONFIG reservation and owner acquisition. It
 * must be side-effect-free, safe for concurrent task/ISR callers, and must not access
 * UART/DMA state that requires the service owner. `AdvanceConfig()`,
 * `CompleteConfig()`, `AdvanceRecovery()`, and `CompleteRecovery()` obey
 * `UartDmaControlResult`; the model also passes whether a LibXR active TX exists so the
 * backend never infers software ownership from idle DMA/UART registers. Every PENDING
 * path is idempotent and owns a guaranteed future
 * carrier. Complete hooks return `UartDmaControlProgress`. After an Advance hook returns
 * COMPLETED, the model publishes its own CONTROL_READY continuation together with the
 * returned old-TX COMPLETE fact, if any. That forces one hardware-quiescent service
 * snapshot to retire the old active generation. The corresponding Complete hook then
 * restarts RX, retained active TX is restarted when present, and only then is the
 * RX/config gate released. This internal continuation does not depend on a later Write,
 * IRQ, or external scheduler. Every hook reached with `in_isr == true` must be ISR-safe
 * and non-blocking.
 *
 * CONFIG may upgrade a recovery transaction while its Advance hook is pending. The
 * already-selected recovery Advance hook remains responsible for reaching quiescence;
 * after it completes, CONFIG applies its payload without running `CompleteRecovery()` or
 * restarting the old data path in between. CONFIG reserved after `CompleteRecovery()`
 * has already begun is retained as the next transaction instead, because that backend
 * restart has crossed its linearization point.
 *
 * @tparam Backend Statically bound platform backend.
 * @tparam Policy Per-instance serialized execution policy.
 */
template <typename Backend, typename Policy>
class UartDmaModel
{
 public:
  UartDmaModel(Backend& backend, Policy& policy, WritePort& port, RawData storage)
      : backend_(backend), policy_(policy), port_(port), buffers_(storage)
  {
    REQUIRE(port_.queue_info_ != nullptr);
    if (port_.queue_data_ == nullptr)
    {
      REQUIRE(buffers_.Size() == 0U);
    }
    else
    {
      REQUIRE(port_.queue_data_->MaxSize() <= buffers_.Size());
    }

    // The first staged payload uses block 0, then promotion flips it to active.
    buffers_.SetActiveBlock(true);
  }

  /** Publish WRITE and return the current call's synchronous result when identifiable. */
  ErrorCode Submit(bool in_isr)
  {
    SubmitContext context{};
    (void)policy_.Invoke(EventMask(UartDmaEvent::WRITE),
                         [this, in_isr, &context](uint32_t events) noexcept
                         { return ServiceEvents(events, in_isr, &context); });
    return context.result;
  }

  /**
   * @brief Validate, reserve, store, and publish one complete CONFIG transaction.
   *
   * Validation precedes all model admission; see the pure `ValidateConfig()` backend
   * contract in the class documentation.
   */
  ErrorCode SetConfig(UART::Configuration config, bool in_isr)
  {
    const ErrorCode validation = backend_.ValidateConfig(config);
    if (validation != ErrorCode::OK)
    {
      return validation;
    }
    if (!rx_config_gate_.TryReserveConfig())
    {
      return ErrorCode::BUSY;
    }

    requested_config_ = config;
    rx_config_gate_.PublishConfig();
    Invoke(UartDmaEvent::CONFIG, in_isr);
    return ErrorCode::OK;
  }

  /**
   * @brief Publish one authoritative whole-transfer completion.
   *
   * A backend must not publish this merely because a partial prefix has drained. During
   * control stop it is valid only after the backend has already proved that the old DMA
   * payload completed without a TX error. COMPLETE is also a control carrier, so that
   * authoritative terminal can finish a pending quiescence transaction.
   */
  void OnTransferDone(bool in_isr) { Invoke(UartDmaEvent::COMPLETE, in_isr); }

  /** Publish one authoritative runtime UART/DMA error. */
  void OnTransferError(bool in_isr) { Invoke(UartDmaEvent::ERROR, in_isr); }

  /**
   * @brief Run one RX fragment after acquiring the complete RX/CONFIG gate.
   *
   * The backend may read and clear interrupt status before this call. `handler` contains
   * the first DMA position/descriptor access and all byte delivery. A rejected fragment
   * is intentionally dropped and also acts as a carrier for waiting control work.
   *
   * @return true when `handler` ran; false when CONFIG or recovery closed RX admission.
   */
  template <typename Handler>
  bool ProcessRx(bool in_isr, Handler&& handler)
  {
    uint32_t events = 0U;
    const bool admitted = ProcessRxInIrqSource(events, std::forward<Handler>(handler));
    InvokeEvents(events, in_isr);
    return admitted;
  }

  /**
   * @brief Gate RX work inside an `InvokeIrq()` source without nested control dispatch.
   *
   * Any required CONTROL_READY fact is ORed into `source_events`. The IRQ source must
   * return that mask together with all COMPLETE/ERROR facts read from the same hardware
   * snapshot. This prevents CONFIG from retiring the old generation between RX handling
   * and a later terminal fact from that snapshot.
   */
  template <typename Handler>
  bool ProcessRxInIrqSource(uint32_t& source_events, Handler&& handler)
  {
    ASSERT((source_events & ~ALL_EVENTS) == 0U);
    if (!rx_config_gate_.TryEnterRx())
    {
      source_events |= EventMask(UartDmaEvent::CONTROL_READY);
      return false;
    }

    Handler& handler_ref = handler;
    handler_ref();

    if (rx_config_gate_.LeaveRx())
    {
      source_events |= EventMask(UartDmaEvent::CONTROL_READY);
    }
    return true;
  }

  /** Publish a real backend stop-completion carrier. */
  void OnStopDone(bool in_isr) { Invoke(UartDmaEvent::STOP_DONE, in_isr); }

  /**
   * @brief Admit an owned raw IRQ before its first protected status access.
   *
   * `source` may read/acknowledge hardware and return `UartDmaEvent` facts. Any RX
   * position/descriptor consumption inside it must use `ProcessRxInIrqSource()` so a
   * waiting control transaction joins the same returned snapshot.
   */
  template <typename Source>
  bool InvokeIrq(Source&& source, bool in_isr)
  {
    return policy_.InvokeIrq(std::forward<Source>(source),
                             [this, in_isr](uint32_t events) noexcept
                             { return ServiceEvents(events, in_isr, nullptr); });
  }

  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  static constexpr uint32_t EventMask(UartDmaEvent event)
  {
    return static_cast<uint32_t>(event);
  }

 private:
  enum class ControlState : uint8_t
  {
    NORMAL = 0U,
    CONTROL_STOPPING = 1U,
    CONTROL_RESTARTING = 2U,
    CONTROL_COMPLETING = 3U,
  };

  enum class ControlIntent : uint8_t
  {
    RECOVERY = 0U,
    CONFIG = 1U,
  };

  /** Backend Advance hook that initiated the current stop, not an execution owner. */
  enum class ControlStopOrigin : uint8_t
  {
    RECOVERY = 0U,
    CONFIG = 1U,
  };

  enum class StageResult : uint32_t
  {
    EMPTY = 0U,
    BLOCKED = 1U,
    STAGED = 2U,
  };

  enum class StartPendingResult : uint32_t
  {
    BLOCKED = 0U,
    FAILED = 1U,
    STARTED = 2U,
  };

  struct SubmitContext
  {
    ErrorCode result = ErrorCode::PENDING;
    bool resolved = false;
    bool synchronous_completion_allowed = true;
  };

  static constexpr uint32_t ALL_EVENTS =
      EventMask(UartDmaEvent::WRITE) | EventMask(UartDmaEvent::COMPLETE) |
      EventMask(UartDmaEvent::ERROR) | EventMask(UartDmaEvent::CONFIG) |
      EventMask(UartDmaEvent::STOP_DONE) | EventMask(UartDmaEvent::CONTROL_READY);

  static constexpr bool HasEvent(uint32_t events, UartDmaEvent event)
  {
    return (events & EventMask(event)) != 0U;
  }

  static constexpr bool IsControlCarrier(uint32_t events)
  {
    return HasEvent(events, UartDmaEvent::STOP_DONE) ||
           HasEvent(events, UartDmaEvent::CONTROL_READY) ||
           HasEvent(events, UartDmaEvent::COMPLETE) ||
           HasEvent(events, UartDmaEvent::ERROR);
  }

  static uint32_t ControlContinuation(UartDmaControlResult result)
  {
    ASSERT(result.IsCompleted());
    uint32_t events = EventMask(UartDmaEvent::CONTROL_READY);
    if (result.old_tx_terminal == UartOldTxTerminal::COMPLETE)
    {
      events |= EventMask(UartDmaEvent::COMPLETE);
    }
    return events;
  }

  void Invoke(UartDmaEvent event, bool in_isr) { InvokeEvents(EventMask(event), in_isr); }

  void InvokeEvents(uint32_t events, bool in_isr)
  {
    if (events == 0U)
    {
      return;
    }
    ASSERT((events & ~ALL_EVENTS) == 0U);

    (void)policy_.Invoke(events, [this, in_isr](uint32_t snapshot) noexcept
                         { return ServiceEvents(snapshot, in_isr, nullptr); });
  }

  uint32_t ServiceEvents(uint32_t events, bool in_isr, SubmitContext* submit) noexcept
  {
    ASSERT((events & ~ALL_EVENTS) == 0U);

    const bool complete_seen = HasEvent(events, UartDmaEvent::COMPLETE);
    if (complete_seen)
    {
      (void)ReleaseActive();
    }

    const bool config_seen = HasEvent(events, UartDmaEvent::CONFIG);
    if (control_state_ == ControlState::CONTROL_STOPPING)
    {
      if (config_seen)
      {
        DisableSynchronousCompletion(submit);
        control_intent_ = ControlIntent::CONFIG;
      }
      if (config_seen || IsControlCarrier(events))
      {
        return AdvanceControl(in_isr);
      }
      return 0U;
    }

    if (control_state_ == ControlState::CONTROL_RESTARTING)
    {
      if (config_seen && control_intent_ == ControlIntent::RECOVERY)
      {
        DisableSynchronousCompletion(submit);
        control_intent_ = ControlIntent::CONFIG;
        control_stop_origin_ = ControlStopOrigin::CONFIG;
        control_state_ = ControlState::CONTROL_STOPPING;
        return AdvanceControl(in_isr);
      }
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (control_state_ == ControlState::CONTROL_COMPLETING)
    {
      if (config_seen)
      {
        DisableSynchronousCompletion(submit);
      }
      return IsControlCarrier(events) ? CompleteControl(in_isr) : 0U;
    }

    if (config_seen)
    {
      return BeginConfig(in_isr, submit);
    }

    if (HasEvent(events, UartDmaEvent::ERROR))
    {
      return BeginRecovery(in_isr);
    }

    bool progress = HasEvent(events, UartDmaEvent::WRITE);
    if (complete_seen)
    {
      progress = true;
    }

    return progress ? Progress(in_isr, submit) : 0U;
  }

  uint32_t BeginConfig(bool in_isr, SubmitContext* submit)
  {
    DisableSynchronousCompletion(submit);
    control_state_ = ControlState::CONTROL_STOPPING;
    control_intent_ = ControlIntent::CONFIG;
    control_stop_origin_ = ControlStopOrigin::CONFIG;
    return AdvanceControl(in_isr);
  }

  uint32_t BeginRecovery(bool in_isr)
  {
    control_state_ = ControlState::CONTROL_STOPPING;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    return AdvanceControl(in_isr);
  }

  uint32_t AdvanceControl(bool in_isr)
  {
    const bool admitted = control_intent_ == ControlIntent::CONFIG
                              ? rx_config_gate_.TryEnterConfig()
                              : rx_config_gate_.TryEnterRecovery();
    if (!admitted)
    {
      return 0U;
    }

    const UartDmaControlResult result =
        control_stop_origin_ == ControlStopOrigin::CONFIG
            ? backend_.AdvanceConfig(requested_config_, active_length_ != 0U, in_isr)
            : backend_.AdvanceRecovery(active_length_ != 0U, in_isr);
    if (!result.IsCompleted())
    {
      return 0U;
    }

    if (control_intent_ == ControlIntent::CONFIG &&
        control_stop_origin_ == ControlStopOrigin::RECOVERY)
    {
      // Preserve the recovery hook's typed terminal as a service snapshot before the
      // CONFIG hook observes active_tx and applies the payload on the same quiescence.
      control_stop_origin_ = ControlStopOrigin::CONFIG;
      return ControlContinuation(result);
    }

    control_state_ = ControlState::CONTROL_RESTARTING;
    return ControlContinuation(result);
  }

  uint32_t CompleteControl(bool in_isr)
  {
    ASSERT(control_state_ == ControlState::CONTROL_RESTARTING ||
           control_state_ == ControlState::CONTROL_COMPLETING);
    control_state_ = ControlState::CONTROL_COMPLETING;
    if (control_intent_ == ControlIntent::CONFIG)
    {
      return backend_.CompleteConfig(in_isr) == UartDmaControlProgress::COMPLETED
                 ? FinishConfig(in_isr)
                 : 0U;
    }
    return backend_.CompleteRecovery(in_isr) == UartDmaControlProgress::COMPLETED
               ? FinishRecovery(in_isr)
               : 0U;
  }

  uint32_t FinishRecovery(bool in_isr)
  {
    RestartActiveAfterRecovery(in_isr);
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveRecovery();
    uint32_t events = EventMask(UartDmaEvent::WRITE);
    if (rx_config_gate_.ConfigRequested())
    {
      // CONFIG may have been consumed while CompleteRecovery() was pending. It becomes
      // a separate transaction only after the current recovery has fully completed.
      events |= EventMask(UartDmaEvent::CONFIG);
    }
    return events;
  }

  uint32_t FinishConfig(bool in_isr)
  {
    RestartActiveDuringConfig(in_isr);
    control_state_ = ControlState::NORMAL;
    control_intent_ = ControlIntent::RECOVERY;
    control_stop_origin_ = ControlStopOrigin::RECOVERY;
    rx_config_gate_.LeaveConfig();
    return EventMask(UartDmaEvent::WRITE);
  }

  uint32_t Progress(bool in_isr, SubmitContext* submit)
  {
    if (active_length_ == 0U)
    {
      if (!pending_valid_)
      {
        const StageResult stage = StageNextPending(in_isr);
        if (stage == StageResult::EMPTY || stage == StageResult::BLOCKED)
        {
          return 0U;
        }
      }

      const StartPendingResult start = TryStartPending(in_isr, submit);
      if (start == StartPendingResult::BLOCKED)
      {
        return 0U;
      }
      if (start == StartPendingResult::FAILED)
      {
        // Re-enter through the service snapshot boundary so CONFIG/ERROR published by
        // the failed record's callback keep their priority over the next queued start.
        return EventMask(UartDmaEvent::WRITE);
      }
    }

    if ((active_length_ != 0U) && !pending_valid_)
    {
      (void)StageNextPending(in_isr);
    }
    return 0U;
  }

  StageResult StageNextPending(bool in_isr)
  {
    if (pending_valid_)
    {
      return StageResult::STAGED;
    }
    if (!rx_config_gate_.TryEnterTx())
    {
      return StageResult::BLOCKED;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      rx_config_gate_.LeaveTx();
      return StageResult::EMPTY;
    }

    ASSERT(port_.queue_data_ != nullptr);
    if (port_.queue_data_ == nullptr)
    {
      rx_config_gate_.LeaveTx();
      return StageResult::EMPTY;
    }

    REQUIRE_FROM_CALLBACK(info.data.size_ <= buffers_.Size(), in_isr);
    const ErrorCode result =
        port_.queue_data_->PopBatch(buffers_.PendingBuffer(), info.data.size_);
    ASSERT(result == ErrorCode::OK);
    if (result != ErrorCode::OK)
    {
      rx_config_gate_.LeaveTx();
      return StageResult::EMPTY;
    }

    // Payload and metadata length are complete before pending becomes visible.
    pending_valid_ = true;
    rx_config_gate_.LeaveTx();
    return StageResult::STAGED;
  }

  StartPendingResult TryStartPending(bool in_isr, SubmitContext* submit)
  {
    ASSERT(pending_valid_);
    ASSERT(active_length_ == 0U);
    if (!rx_config_gate_.TryEnterTx())
    {
      return StartPendingResult::BLOCKED;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      ASSERT(false);
      rx_config_gate_.LeaveTx();
      return StartPendingResult::FAILED;
    }

    const bool synchronous_submission =
        (submit != nullptr) && submit->synchronous_completion_allowed &&
        !submit->resolved && (port_.queue_info_->Size() == 1U);

    // Publish the complete active state before the backend can synchronously callback.
    buffers_.FlipActiveBlock();
    pending_valid_ = false;
    active_length_ = info.data.size_;

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);

    if (!PopInfo(info))
    {
      ASSERT(false);
      ClearActive();
      rx_config_gate_.LeaveTx();
      return StartPendingResult::FAILED;
    }

    if (result == UartDmaTxStartResult::FAILED)
    {
      ClearActive();
      buffers_.FlipActiveBlock();
      rx_config_gate_.LeaveTx();
      CompleteRecord(in_isr, ErrorCode::FAILED, info, synchronous_submission, submit);
      return StartPendingResult::FAILED;
    }

    rx_config_gate_.LeaveTx();
    CompleteRecord(in_isr, ErrorCode::OK, info, synchronous_submission, submit);
    return StartPendingResult::STARTED;
  }

  void CompleteRecord(bool in_isr, ErrorCode result, WriteInfoBlock& info,
                      bool synchronous_submission, SubmitContext* submit)
  {
    if (synchronous_submission)
    {
      submit->result = result;
      submit->resolved = true;
      return;
    }
    port_.Finish(in_isr, result, info);
  }

  static void DisableSynchronousCompletion(SubmitContext* submit)
  {
    if (submit != nullptr)
    {
      submit->synchronous_completion_allowed = false;
    }
  }

  bool PopInfo(WriteInfoBlock& info)
  {
    const ErrorCode result = port_.queue_info_->Pop(info);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool ReleaseActive()
  {
    if (active_length_ == 0U)
    {
      return false;
    }
    ClearActive();
    return true;
  }

  void ClearActive() { active_length_ = 0U; }

  void RestartActiveDuringConfig(bool in_isr)
  {
    if (active_length_ == 0U)
    {
      return;
    }

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);
    REQUIRE_FROM_CALLBACK(result == UartDmaTxStartResult::STARTED, in_isr);
  }

  void RestartActiveAfterRecovery(bool in_isr)
  {
    if (active_length_ == 0U)
    {
      return;
    }
    if (!rx_config_gate_.TryEnterTx())
    {
      // A concurrently reserved CONFIG won the TX admission boundary. Its publication
      // is the guaranteed carrier; the retained active payload stays stopped for it.
      ASSERT(rx_config_gate_.ConfigRequested());
      return;
    }

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock(), in_isr);
    rx_config_gate_.LeaveTx();
    REQUIRE_FROM_CALLBACK(result == UartDmaTxStartResult::STARTED, in_isr);
  }

  Backend& backend_;
  Policy& policy_;
  WritePort& port_;
  DoubleBufferStorage buffers_;
  UartRxConfigGate rx_config_gate_;
  UART::Configuration requested_config_{};
  ControlState control_state_ = ControlState::NORMAL;
  ControlIntent control_intent_ = ControlIntent::RECOVERY;
  ControlStopOrigin control_stop_origin_ = ControlStopOrigin::RECOVERY;
  size_t active_length_ = 0U;
  bool pending_valid_ = false;
};

}  // namespace LibXR
