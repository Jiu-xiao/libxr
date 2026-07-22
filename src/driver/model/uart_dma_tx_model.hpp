#pragma once

#include <cstddef>
#include <cstdint>

#include "double_buffer_storage.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/** Result of one backend DMA-start attempt. */
enum class UartDmaTxStartResult : uint32_t
{
  STARTED = 0U,
  FAILED = 1U,
};

/** Result of one backend CONFIG or runtime-recovery advancement. */
enum class UartDmaControlResult : uint32_t
{
  COMPLETED = 0U,
  PENDING = 1U,
};

/** Coalescible facts consumed by one UART serialized service. */
enum class UartDmaTxEvent : uint32_t
{
  WRITE = 1U << 0U,
  COMPLETE = 1U << 1U,
  ERROR = 1U << 2U,
  CONFIG = 1U << 3U,
  STOP_DONE = 1U << 4U,
  CONTROL_READY = 1U << 5U,
};

/**
 * @brief One-active/one-pending UART DMA TX business model.
 *
 * An external per-UART execution policy owns the only `SerializedService`. WRITE,
 * COMPLETE, ERROR, CONFIG, and asynchronous stop completion enter through that policy.
 * Only the service owner mutates the ordinary active/pending fields, dequeues WritePort,
 * starts DMA, performs recovery/configuration, or completes queued records.
 *
 * The pending payload is copied out of the public byte queue while its metadata remains
 * at the metadata-queue head. Promotion publishes active state before `StartDmaTx()`.
 * A terminal callback raised before that function returns only merges another service
 * event and is therefore processed after the owner commits STARTED or FAILED.
 *
 * `StartDmaTx() == FAILED` is record-local and never requests CONFIG. Runtime ERROR asks
 * the backend to recover the data path, releases the already-accepted active software
 * record after quiescence, preserves pending, and resumes queued work. CONFIG fixes the
 * metadata prefix at admission, applies the one accepted configuration after quiescence,
 * fails only that fixed prefix, and republishes WRITE. CONFIG admission and payload
 * publication are owned by the backend's RX/CONFIG gate.
 *
 * @tparam Backend Statically bound platform backend.
 */
template <typename Backend>
class UartDmaTxModel
{
 public:
  UartDmaTxModel(Backend& backend, WritePort& port, RawData storage)
      : backend_(backend), port_(port), buffers_(storage)
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
  template <typename Policy>
  ErrorCode Submit(Policy& policy, bool in_isr)
  {
    SubmitContext context{};
    (void)policy.Invoke(EventMask(UartDmaTxEvent::WRITE),
                        [this, in_isr, &context](uint32_t events) noexcept
                        { return ServiceTx(events, in_isr, &context); });
    return context.result;
  }

  /** Publish one authoritative normal DMA completion. */
  template <typename Policy>
  void OnTransferDone(Policy& policy, bool in_isr)
  {
    Invoke(policy, UartDmaTxEvent::COMPLETE, in_isr);
  }

  /** Publish one authoritative runtime UART/DMA error. */
  template <typename Policy>
  void OnTransferError(Policy& policy, bool in_isr)
  {
    Invoke(policy, UartDmaTxEvent::ERROR, in_isr);
  }

  /** Publish the backend's one admitted CONFIG request. */
  template <typename Policy>
  void RequestConfig(Policy& policy, bool in_isr)
  {
    (void)policy.InvokeConfig(EventMask(UartDmaTxEvent::CONFIG), in_isr,
                              [this, in_isr](uint32_t events) noexcept
                              { return ServiceTx(events, in_isr, nullptr); });
  }

  /** Publish a real backend stop-completion carrier. */
  template <typename Policy>
  void OnStopDone(Policy& policy, bool in_isr)
  {
    Invoke(policy, UartDmaTxEvent::STOP_DONE, in_isr);
  }

  /** Retry CONFIG or recovery after the last RX fragment leaves its control gate. */
  template <typename Policy>
  void OnControlReady(Policy& policy, bool in_isr)
  {
    Invoke(policy, UartDmaTxEvent::CONTROL_READY, in_isr);
  }

  /**
   * @brief Process an already-owned event snapshot.
   *
   * A backend raw-IRQ adapter may use this as the handler passed to the same execution
   * policy that owns normal WRITE/CONFIG calls.
   *
   * @return Continuation events to merge before the owner attempts release.
   */
  uint32_t Service(uint32_t events, bool in_isr) noexcept
  {
    return ServiceTx(events, in_isr, nullptr);
  }

  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  static constexpr uint32_t EventMask(UartDmaTxEvent event)
  {
    return static_cast<uint32_t>(event);
  }

 private:
  enum class ControlPhase : uint32_t
  {
    NORMAL = 0U,
    ERROR_STOPPING = 1U,
    CONFIG_STOPPING = 2U,
  };

  struct SubmitContext
  {
    ErrorCode result = ErrorCode::PENDING;
    bool resolved = false;
  };

  static constexpr uint32_t ALL_EVENTS =
      EventMask(UartDmaTxEvent::WRITE) | EventMask(UartDmaTxEvent::COMPLETE) |
      EventMask(UartDmaTxEvent::ERROR) | EventMask(UartDmaTxEvent::CONFIG) |
      EventMask(UartDmaTxEvent::STOP_DONE) | EventMask(UartDmaTxEvent::CONTROL_READY);

  static constexpr bool HasEvent(uint32_t events, UartDmaTxEvent event)
  {
    return (events & EventMask(event)) != 0U;
  }

  template <typename Policy>
  void Invoke(Policy& policy, UartDmaTxEvent event, bool in_isr)
  {
    (void)policy.Invoke(EventMask(event), [this, in_isr](uint32_t events) noexcept
                        { return ServiceTx(events, in_isr, nullptr); });
  }

  uint32_t ServiceTx(uint32_t events, bool in_isr, SubmitContext* submit) noexcept
  {
    ASSERT((events & ~ALL_EVENTS) == 0U);

    if (control_phase_ == ControlPhase::CONFIG_STOPPING)
    {
      if (HasEvent(events, UartDmaTxEvent::STOP_DONE) ||
          HasEvent(events, UartDmaTxEvent::CONTROL_READY) ||
          HasEvent(events, UartDmaTxEvent::ERROR))
      {
        if (backend_.ApplyPendingConfig(in_isr) == UartDmaControlResult::COMPLETED)
        {
          return FinishConfig(in_isr);
        }
      }
      return 0U;
    }

    if (control_phase_ == ControlPhase::ERROR_STOPPING)
    {
      if (HasEvent(events, UartDmaTxEvent::CONFIG))
      {
        config_prefix_count_ = port_.queue_info_->Size();
        control_phase_ = ControlPhase::CONFIG_STOPPING;
        if (backend_.ApplyPendingConfig(in_isr) == UartDmaControlResult::COMPLETED)
        {
          return FinishConfig(in_isr);
        }
        return 0U;
      }

      if (HasEvent(events, UartDmaTxEvent::STOP_DONE) ||
          HasEvent(events, UartDmaTxEvent::CONTROL_READY) ||
          HasEvent(events, UartDmaTxEvent::ERROR))
      {
        if (backend_.RecoverDataPath(in_isr) == UartDmaControlResult::COMPLETED)
        {
          return FinishRecovery();
        }
      }
      return 0U;
    }

    if (HasEvent(events, UartDmaTxEvent::CONFIG))
    {
      config_prefix_count_ = port_.queue_info_->Size();
      control_phase_ = ControlPhase::CONFIG_STOPPING;
      if (backend_.ApplyPendingConfig(in_isr) == UartDmaControlResult::COMPLETED)
      {
        return FinishConfig(in_isr);
      }
      return 0U;
    }

    if (HasEvent(events, UartDmaTxEvent::ERROR))
    {
      control_phase_ = ControlPhase::ERROR_STOPPING;
      if (backend_.RecoverDataPath(in_isr) == UartDmaControlResult::COMPLETED)
      {
        return FinishRecovery();
      }
      return 0U;
    }

    bool progress = HasEvent(events, UartDmaTxEvent::WRITE);
    if (HasEvent(events, UartDmaTxEvent::COMPLETE))
    {
      (void)ReleaseActive();
      progress = true;
    }

    return progress ? Progress(in_isr, submit) : 0U;
  }

  uint32_t FinishRecovery()
  {
    (void)ReleaseActive();
    control_phase_ = ControlPhase::NORMAL;
    return EventMask(UartDmaTxEvent::WRITE);
  }

  uint32_t FinishConfig(bool in_isr)
  {
    (void)ReleaseActive();

    if (pending_valid_)
    {
      ASSERT(config_prefix_count_ > 0U);
      pending_valid_ = false;

      WriteInfoBlock info{};
      if (!PopInfo(info))
      {
        ASSERT(false);
        return 0U;
      }
      --config_prefix_count_;
      port_.Finish(in_isr, ErrorCode::FAILED, info);
    }

    while (config_prefix_count_ > 0U)
    {
      WriteInfoBlock info{};
      if (port_.queue_info_->Peek(info) != ErrorCode::OK)
      {
        ASSERT(false);
        return 0U;
      }

      ASSERT(port_.queue_data_ != nullptr);
      if (port_.queue_data_ == nullptr)
      {
        return 0U;
      }

      const ErrorCode data_result = port_.queue_data_->PopBatch(nullptr, info.data.size_);
      ASSERT(data_result == ErrorCode::OK);
      if ((data_result != ErrorCode::OK) || !PopInfo(info))
      {
        ASSERT(false);
        return 0U;
      }

      --config_prefix_count_;
      port_.Finish(in_isr, ErrorCode::FAILED, info);
    }

    control_phase_ = ControlPhase::NORMAL;
    backend_.ReleaseConfigAdmission(in_isr);
    return EventMask(UartDmaTxEvent::WRITE);
  }

  uint32_t Progress(bool in_isr, SubmitContext* submit)
  {
    if (active_length_ == 0U)
    {
      if (!pending_valid_ && !StageNextPending(in_isr))
      {
        return 0U;
      }

      if (!TryStartPending(in_isr, submit))
      {
        // Re-enter through the service snapshot boundary so CONFIG/ERROR published by
        // the failed record's callback keep their priority over the next queued start.
        return EventMask(UartDmaTxEvent::WRITE);
      }
    }

    if ((active_length_ != 0U) && !pending_valid_)
    {
      (void)StageNextPending(in_isr);
    }
    return 0U;
  }

  bool StageNextPending(bool in_isr)
  {
    if (pending_valid_)
    {
      return false;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return false;
    }

    ASSERT(port_.queue_data_ != nullptr);
    if (port_.queue_data_ == nullptr)
    {
      return false;
    }

    REQUIRE_FROM_CALLBACK(info.data.size_ <= buffers_.Size(), in_isr);
    const ErrorCode result =
        port_.queue_data_->PopBatch(buffers_.PendingBuffer(), info.data.size_);
    ASSERT(result == ErrorCode::OK);
    if (result != ErrorCode::OK)
    {
      return false;
    }

    // Payload and metadata length are complete before pending becomes visible.
    pending_valid_ = true;
    return true;
  }

  bool TryStartPending(bool in_isr, SubmitContext* submit)
  {
    ASSERT(pending_valid_);
    ASSERT(active_length_ == 0U);

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      ASSERT(false);
      return false;
    }

    const bool synchronous_submission =
        (submit != nullptr) && !submit->resolved && (port_.queue_info_->Size() == 1U);

    // Publish the complete active state before the backend can synchronously callback.
    buffers_.FlipActiveBlock();
    pending_valid_ = false;
    active_length_ = info.data.size_;

    const UartDmaTxStartResult result = backend_.StartDmaTx(
        buffers_.ActiveBuffer(), active_length_, buffers_.ActiveBlock());

    if (!PopInfo(info))
    {
      ASSERT(false);
      ClearActive();
      return false;
    }

    if (result == UartDmaTxStartResult::FAILED)
    {
      ClearActive();
      buffers_.FlipActiveBlock();
      CompleteRecord(in_isr, ErrorCode::FAILED, info, synchronous_submission, submit);
      return false;
    }

    CompleteRecord(in_isr, ErrorCode::OK, info, synchronous_submission, submit);
    return true;
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

  Backend& backend_;
  WritePort& port_;
  DoubleBufferStorage buffers_;
  ControlPhase control_phase_ = ControlPhase::NORMAL;
  size_t active_length_ = 0U;
  size_t config_prefix_count_ = 0U;
  bool pending_valid_ = false;
};

}  // namespace LibXR
