#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "double_buffer.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"
#include "serialized_service.hpp"

namespace LibXR
{

/**
 * @brief Result of one synchronous backend DMA-start attempt.
 *
 * `RETRY` is valid only before the backend has started or otherwise committed the DMA
 * transfer. The model keeps the queue record and buffer ownership unchanged and expects
 * a later `ResumeStart()`. `FAILED` is terminal for this attempt and also guarantees
 * that no DMA remains active. A failure of the current submission is returned through
 * its `SubmitContext`; an older retained record is completed through `WritePort::Finish`.
 * `STARTED` transfers the candidate buffer to hardware. For each accepted start, the
 * platform must dispatch at most one authoritative terminal snapshot; it must clear
 * that hardware status before a later start can make the same status observable as the
 * later transfer's completion.
 */
enum class UartDmaTxStartResult : uint32_t
{
  STARTED = 0U,
  RETRY = 1U,
  FAILED = 2U,
};

/**
 * @brief UART 单次 DMA 发送执行模型 / UART one-shot DMA TX execution model
 *
 * `Submit()`、正常完成和错误恢复入口只发布可合并事件。`SerializedService` 选出的
 * 唯一 owner 负责 active/pending buffer、WritePort 出队、DMA 启动以及 Operation
 * 完成，避免提交线程与完成 ISR 并发推进双缓冲状态。
 * `Submit()`, normal completion, and recovered-error entries only publish coalesced
 * events. The sole owner selected by `SerializedService` controls active/pending
 * buffers, WritePort dequeue, DMA start, and Operation completion, preventing submitters
 * and completion ISRs from advancing the double-buffer state concurrently.
 *
 * @warning Service re-entry is flattened, but `WritePort` producer ownership is a
 * separate boundary. `Finish()` for an older record, or for a current record drained by
 * same-round CONFIG recovery, may run while a direct or Stream producer owns the port.
 * A callback that writes the same port can therefore receive `BUSY`. Such callbacks must
 * handle/retry `BUSY`; serialization alone does not enqueue a rejected re-entrant write.
 *
 * @tparam Backend 静态绑定的平台后端类型 / Statically bound platform backend type
 */
template <typename Backend>
class UartDmaTxModel
{
 public:
  /**
   * @brief 绑定平台后端、写端口和 DMA 双缓冲区 / Bind the backend, write port, and
   * DMA double buffer
   */
  UartDmaTxModel(Backend& backend, WritePort& port, RawData storage)
      : backend_(backend), port_(port), buffers_(storage)
  {
    REQUIRE(port_.queue_data_ != nullptr);
    REQUIRE(port_.queue_data_->MaxSize() <= buffers_.Size());
  }

  /**
   * @brief 发布 WRITE 事件并推进队首请求 / Publish WRITE and advance the head request
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   * @return 当前请求同步启动时返回 `OK`，留在流水中时返回 `PENDING`，同步启动失败
   * 时返回错误 / `OK` for a synchronous start, `PENDING` while retained by the
   * pipeline. A defensive backend start failure for this submission is returned
   * synchronously. An ordinary direct WritePort callback runs after the producer lock is
   * released unless same-round CONFIG recovery drains that submission. Stream retains
   * producer ownership until its Commit path returns. In either exceptional case, a
   * callback that re-enters the same port may receive `BUSY`.
   */
  ErrorCode Submit(bool in_isr)
  {
    SubmitContext context{port_.CurrentSubmissionId(), ErrorCode::PENDING};
    (void)service_.Invoke(EventMask(TxEvent::WRITE),
                          [this, in_isr, &context](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, &context); });
    return context.result;
  }

  /**
   * @brief 发布正常完成事件 / Publish a normal completion event
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   */
  void OnTransferDone(bool in_isr)
  {
    (void)service_.Invoke(EventMask(TxEvent::COMPLETE),
                          [this, in_isr](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr); });
  }

  /**
   * @brief 在平台恢复旧 DMA 后发布错误终止事件 / Publish an error terminal event
   * after platform recovery
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   * @note 平台必须先停止/复位旧 DMA 并清除其迟到完成源，再调用本接口。The platform
   * must stop/reset the old DMA and clear its stale completion source before this call.
   */
  void OnTransferError(bool in_isr)
  {
    (void)service_.Invoke(EventMask(TxEvent::ERROR),
                          [this, in_isr](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr); });
  }

  /**
   * @brief 发布最高优先级配置事件 / Publish the highest-priority configuration event
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   *
   * 平台后端负责保存最新配置 payload。本入口只发布可合并事件；CONFIG owner 会停止并
   * 重配硬件、终止旧 TX 前缀，然后让 CONFIG 期间追加的请求在后续轮次继续推进。
   * The platform backend owns the latest configuration payload. This entry only
   * publishes the coalesced event; the CONFIG owner reconfigures hardware, aborts the
   * old TX prefix, and leaves requests appended during CONFIG for later service rounds.
   */
  void RequestConfig(bool in_isr)
  {
    backend_.OnConfigRequested();
    ResumeConfig(in_isr);
  }

  /**
   * @brief Continue an already claimed asynchronous configuration transaction.
   * @param in_isr Whether the current caller is in an ISR.
   *
   * Unlike `RequestConfig()`, this entry does not publish a new backend configuration
   * request. It is intended for a platform completion callback that resumes the same
   * CONFIG transaction after an asynchronous hardware quiesce step.
   */
  void ResumeConfig(bool in_isr)
  {
    (void)service_.Invoke(EventMask(TxEvent::CONFIG),
                          [this, in_isr](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr); });
  }

  /**
   * @brief Retry a TX start deferred by the shared UART hardware gate.
   *
   * The platform must call this after the owner that observed a pending TX-start action
   * has released the hardware gate. RETRY preserves all queue and buffer ownership.
   */
  void ResumeStart(bool in_isr)
  {
    (void)service_.Invoke(EventMask(TxEvent::WRITE),
                          [this, in_isr](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr); });
  }

  /**
   * @brief 查询 DMA 是否持有 active 请求 / Check whether DMA owns an active request
   * @warning 调用方必须与 TX service 串行化。The caller must serialize this query
   * against the TX service.
   */
  [[nodiscard]] bool IsBusy() const { return busy_; }

  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  [[nodiscard]] size_t BufferSize() const { return buffers_.Size(); }

 private:
  enum class TxEvent : uint32_t
  {
    WRITE = 1U << 0U,
    COMPLETE = 1U << 1U,
    ERROR = 1U << 2U,
    CONFIG = 1U << 3U,
  };

  struct SubmitContext
  {
    uint32_t submission_id = 0U;
    ErrorCode result = ErrorCode::PENDING;
  };

  static constexpr uint32_t EventMask(TxEvent event)
  {
    return static_cast<uint32_t>(event);
  }

  static constexpr bool HasEvent(uint32_t events, TxEvent event)
  {
    return (events & EventMask(event)) != 0U;
  }

  void ServiceTx(uint32_t events, bool in_isr, SubmitContext* submit) noexcept
  {
    if (HasEvent(events, TxEvent::CONFIG))
    {
      if (backend_.ConfigRequested())
      {
        const bool resume_tx = ApplyConfig(in_isr);
        if (!config_boundary_valid_ && resume_tx)
        {
          (void)service_.Invoke(EventMask(TxEvent::WRITE),
                                [this, in_isr](uint32_t pending_events) noexcept
                                { ServiceTx(pending_events, in_isr, nullptr); });
        }
        return;
      }

      events &= ~EventMask(TxEvent::CONFIG);
      if (events == 0U)
      {
        return;
      }
    }

    // A CONFIG request may be waiting for RX hardware ownership. Keep later WRITE and
    // terminal notifications coalesced in their level-triggered facts, but do not let
    // them move the fixed old-prefix boundary before CONFIG is retried.
    if (config_boundary_valid_)
    {
      return;
    }

    bool terminal = false;
    if (HasEvent(events, TxEvent::ERROR))
    {
      terminal = ReleaseActive();
    }
    else if (HasEvent(events, TxEvent::COMPLETE))
    {
      terminal = ReleaseActive();
    }

    if (terminal && PromoteAndStartPending(in_isr))
    {
      return;
    }

    // A pending block may still need its first hardware-start attempt after an IRQ
    // owner released the shared hardware gate. Its payload has already left the data
    // queue, so it must be retried before looking at the ordinary queued head.
    if (!busy_ && (active_length_ == 0U) && pending_valid_ &&
        PromoteAndStartPending(in_isr))
    {
      return;
    }

    if (!busy_ && (active_length_ == 0U))
    {
      StartQueuedActive(in_isr, submit);
    }

    if (busy_)
    {
      (void)StageNextPending(in_isr);
    }
  }

  bool PopPayload(uint8_t* destination, size_t size)
  {
    const ErrorCode result = port_.queue_data_->PopBatch(destination, size);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool PopActiveInfo(WriteInfoBlock& info)
  {
    const ErrorCode result = port_.queue_info_->Pop(info);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool StagePending(const WriteInfoBlock& info)
  {
    if (pending_valid_ || !PopPayload(buffers_.PendingBuffer(), info.data.size_))
    {
      return false;
    }

    pending_valid_ = true;
    return true;
  }

  bool StageNextPending(bool in_isr)
  {
    while (!pending_valid_)
    {
      WriteInfoBlock info{};
      if (port_.queue_info_->Peek(info) != ErrorCode::OK)
      {
        return false;
      }

      REQUIRE_FROM_CALLBACK(info.data.size_ <= buffers_.Size(), in_isr);

      return StagePending(info);
    }
    return false;
  }

  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block)
  {
    const auto result = backend_.StartDmaTx(data, size, block);
    using Result = std::remove_cv_t<decltype(result)>;
    if constexpr (std::is_same_v<Result, bool>)
    {
      return result ? UartDmaTxStartResult::STARTED : UartDmaTxStartResult::FAILED;
    }
    else
    {
      static_assert(std::is_same_v<Result, UartDmaTxStartResult>);
      return result;
    }
  }

  UartDmaTxStartResult StartCandidate(uint8_t* data, size_t size, int block)
  {
    ASSERT(size > 0U);
    busy_ = true;
    const UartDmaTxStartResult result = StartDmaTx(data, size, block);
    if (result == UartDmaTxStartResult::STARTED)
    {
      return result;
    }

    busy_ = false;
    return result;
  }

  bool ReleaseActive()
  {
    if (!busy_)
    {
      return false;
    }

    busy_ = false;
    ClearActive();
    return true;
  }

  bool PromoteAndStartPending(bool in_isr)
  {
    if (!pending_valid_)
    {
      return false;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      ASSERT(false);
      return false;
    }

    const int pending_block = buffers_.ActiveBlock() ^ 1;
    const UartDmaTxStartResult result =
        StartCandidate(buffers_.PendingBuffer(), info.data.size_, pending_block);
    if (result == UartDmaTxStartResult::RETRY)
    {
      return true;
    }

    pending_valid_ = false;
    buffers_.FlipActiveBlock();
    if (!PopActiveInfo(info))
    {
      ASSERT(false);
      busy_ = false;
      return false;
    }
    active_length_ = info.data.size_;

    if (result == UartDmaTxStartResult::FAILED)
    {
      busy_ = false;
      ClearActive();
      port_.Finish(in_isr, ErrorCode::FAILED, info);
      RequestConfig(in_isr);
      return true;
    }

    port_.Finish(in_isr, ErrorCode::OK, info);
    (void)StageNextPending(in_isr);
    return true;
  }

  void StartQueuedActive(bool in_isr, SubmitContext* submit)
  {
    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return;
    }

    // A synchronous WriteFun return may complete only the record published by that
    // caller. A later submitter may still advance an older RETRY head, but that head is
    // then processed contextlessly and completed through its own queued operation.
    SubmitContext* current_submit =
        ((submit != nullptr) && (submit->submission_id == info.submission_id)) ? submit
                                                                               : nullptr;

    REQUIRE_FROM_CALLBACK(info.data.size_ <= buffers_.Size(), in_isr);

    if (port_.queue_data_->PeekBatch(buffers_.ActiveBuffer(), info.data.size_) !=
        ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }

    const UartDmaTxStartResult result =
        StartCandidate(buffers_.ActiveBuffer(), info.data.size_, buffers_.ActiveBlock());
    if (result == UartDmaTxStartResult::RETRY)
    {
      return;
    }

    if (!PopPayload(nullptr, info.data.size_) || !PopActiveInfo(info))
    {
      ASSERT(false);
      busy_ = false;
      return;
    }

    active_length_ = info.data.size_;
    if (result == UartDmaTxStartResult::FAILED)
    {
      busy_ = false;
      ClearActive();
    }

    if (current_submit != nullptr)
    {
      current_submit->result =
          (result == UartDmaTxStartResult::STARTED) ? ErrorCode::OK : ErrorCode::FAILED;
    }
    else
    {
      port_.Finish(
          in_isr,
          (result == UartDmaTxStartResult::STARTED) ? ErrorCode::OK : ErrorCode::FAILED,
          info);
    }
    if (result == UartDmaTxStartResult::FAILED)
    {
      RequestConfig(in_isr);
    }
  }

  bool ApplyConfig(bool in_isr)
  {
    // Metadata is the operation publication boundary. Fix the old prefix before any
    // Finish callback can append another request. Payload without metadata belongs to
    // an in-progress producer and is intentionally left for its later WRITE event.
    // metadata 是操作发布边界。在任何 Finish callback 可能追加新请求前固定旧前缀；
    // 只有 payload、尚无 metadata 的生产者仍在发布中，留给其后续 WRITE 事件处理。
    if (!config_boundary_valid_)
    {
      config_prefix_count_ = port_.queue_info_->Size();
      config_boundary_valid_ = true;
    }

    if (!backend_.ApplyPendingConfig(in_isr))
    {
      return false;
    }

    size_t remaining = config_prefix_count_;
    config_prefix_count_ = 0U;
    config_boundary_valid_ = false;

    (void)ReleaseActive();

    if (pending_valid_)
    {
      ASSERT(remaining > 0U);
      pending_valid_ = false;
      WriteInfoBlock info{};
      if (!PopActiveInfo(info))
      {
        ASSERT(false);
        return false;
      }
      --remaining;
      port_.Finish(in_isr, ErrorCode::FAILED, info);
    }

    FailPublishedQueued(in_isr, remaining);
    return backend_.OnConfigApplied(in_isr);
  }

  void FailPublishedQueued(bool in_isr, size_t count)
  {
    for (size_t index = 0U; index < count; ++index)
    {
      WriteInfoBlock info{};
      if (port_.queue_info_->Peek(info) != ErrorCode::OK)
      {
        ASSERT(false);
        return;
      }
      if ((port_.queue_data_->PopBatch(nullptr, info.data.size_) != ErrorCode::OK) ||
          !PopActiveInfo(info))
      {
        ASSERT(false);
        return;
      }
      port_.Finish(in_isr, ErrorCode::FAILED, info);
    }
  }

  void ClearActive() { active_length_ = 0U; }

  Backend& backend_;
  WritePort& port_;
  DoubleBuffer buffers_;
  SerializedService service_{};
  size_t active_length_ = 0U;
  size_t config_prefix_count_ = 0U;
  bool busy_ = false;
  bool pending_valid_ = false;
  bool config_boundary_valid_ = false;
};

}  // namespace LibXR
