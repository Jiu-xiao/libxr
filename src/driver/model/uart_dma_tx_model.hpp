#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "double_buffer_storage.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"
#include "serialized_service.hpp"
#include "uart_hardware_gate.hpp"

namespace LibXR
{

/**
 * @brief Result of one synchronous backend DMA-start attempt.
 *
 * `RETRY` is valid only before the backend has started or otherwise committed the DMA
 * transfer. The model keeps the queue record and buffer ownership unchanged and expects
 * a later `ResumeStart()`. Before returning `RETRY`, the platform must establish that
 * durable continuation. With `UartHardwareGate`, `RETRY` is therefore allowed only when
 * `TryEnterTxStart()` or `TryEnterNestedTxStart()` failed and left `TX_START_PENDING`;
 * after either admission succeeds, the backend must balance its leave and return only
 * `STARTED` or `FAILED`. `FAILED` terminates only the current record: the backend must
 * leave the hardware reusable for a later start, and the model does not implicitly
 * request CONFIG. A failure of the current submission is returned through its
 * `SubmitContext`; an older retained record is completed through `WritePort::Finish`.
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

enum class UartDmaTxTerminalEvent : uint32_t
{
  NONE = 0U,
  COMPLETE = 1U,
  ERROR = 2U,
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
 * separate boundary. `Finish()` may run while a direct or Stream producer owns the
 * port, including when CONFIG drains the current direct submission. A callback that
 * writes the same port can therefore receive `BUSY`. Such callbacks must handle/retry
 * `BUSY`; serialization alone does not enqueue a rejected re-entrant write.
 *
 * @tparam Backend 静态绑定的平台后端类型 / Statically bound platform backend type
 */
template <typename Backend>
class UartDmaTxModel
{
 public:
  using HardwareOwnerContext = UartHardwareGate::OwnerContext;

 private:
  [[nodiscard]] static constexpr bool BackendSupportsHardwareContext()
  {
    return requires(Backend& backend, uint8_t* data, size_t size, int block,
                    HardwareOwnerContext* hardware_context) {
      backend.StartDmaTx(data, size, block, hardware_context);
    };
  }

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

    // No DMA is active yet. Treat block 1 as the notional previous active block so the
    // first staged candidate uses block 0, preserving the established start sequence.
    buffers_.SetActiveBlock(true);
  }

  /**
   * @brief 发布 WRITE 事件并推进队首请求 / Publish WRITE and advance the head request
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   * @return 当前请求同步启动时返回 `OK`，留在流水中时返回 `PENDING`，同步启动失败
   * 时返回错误 / `OK` for a synchronous start, `PENDING` while retained by the
   * pipeline. A defensive backend start failure for this submission is returned
   * synchronously. A callback normally runs after a direct producer unlocks, but CONFIG
   * may drain an older or current direct record while that producer still owns the port.
   * Stream also retains producer ownership until its Commit path returns. In either
   * case, a callback that re-enters the same port may receive `BUSY`.
   */
  ErrorCode Submit(bool in_isr)
  {
    SubmitContext context{ErrorCode::PENDING};
    (void)service_.Invoke(EventMask(TxEvent::WRITE),
                          [this, in_isr, &context](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, &context, nullptr); });
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
                          { ServiceTx(events, in_isr, nullptr, nullptr); });
  }

  /**
   * @brief Publish a normal completion event from the current IRQ hardware owner.
   * @param in_isr Whether the current caller is in an ISR.
   * @param hardware_context Stack-local ownership evidence for this synchronous IRQ call
   * stack. Ownership follows the service invocation that executes the handler, not the
   * event source. The model and backend must not retain the context.
   */
  void OnTransferDone(bool in_isr, HardwareOwnerContext& hardware_context)
    requires(BackendSupportsHardwareContext())
  {
    (void)service_.Invoke(EventMask(TxEvent::COMPLETE),
                          [this, in_isr, &hardware_context](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr, &hardware_context); });
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
                          { ServiceTx(events, in_isr, nullptr, nullptr); });
  }

  /**
   * @brief Publish an error terminal event from the current IRQ hardware owner.
   * @param in_isr Whether the current caller is in an ISR.
   * @param hardware_context Stack-local ownership evidence for this synchronous IRQ call
   * stack. Ownership follows the service invocation that executes the handler, not the
   * event source. The model and backend must not retain the context.
   * @note The platform must stop/reset the old DMA and clear its stale completion source
   * before this call.
   */
  void OnTransferError(bool in_isr, HardwareOwnerContext& hardware_context)
    requires(BackendSupportsHardwareContext())
  {
    (void)service_.Invoke(EventMask(TxEvent::ERROR),
                          [this, in_isr, &hardware_context](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr, &hardware_context); });
  }

  /**
   * @brief 发布最高优先级配置事件 / Publish the highest-priority configuration event
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   *
   * 平台后端负责保存最新配置 payload。本入口只发布可合并事件；CONFIG owner 会停止并
   * 重配硬件、终止旧 TX 前缀，然后让 CONFIG 期间追加的请求在后续轮次继续推进。
   * The platform backend owns the latest configuration payload. This entry publishes
   * the backend request before the coalesced event; the CONFIG owner fixes the old TX
   * prefix, advances an immediate or asynchronous hardware transaction, and leaves
   * requests appended after that boundary for a mandatory post-CONFIG rescan.
   */
  void RequestConfig(bool in_isr)
  {
    backend_.OnConfigRequested();
    ResumeConfig(in_isr);
  }

  /**
   * @brief Continue an already published configuration transaction.
   * @param in_isr Whether the current caller is in an ISR.
   *
   * Unlike `RequestConfig()`, this entry does not create a new backend request. An
   * ownership handoff or asynchronous hardware callback uses it to republish the
   * level-triggered CONFIG obligation. Re-entry is flattened by `SerializedService`.
   */
  void ResumeConfig(bool in_isr)
  {
    (void)service_.Invoke(EventMask(TxEvent::CONFIG),
                          [this, in_isr](uint32_t events) noexcept
                          { ServiceTx(events, in_isr, nullptr, nullptr); });
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
                          { ServiceTx(events, in_isr, nullptr, nullptr); });
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
    ErrorCode result = ErrorCode::PENDING;
    bool synchronous_candidate = false;
  };

  static constexpr uint32_t EventMask(TxEvent event)
  {
    return static_cast<uint32_t>(event);
  }

  static constexpr bool HasEvent(uint32_t events, TxEvent event)
  {
    return (events & EventMask(event)) != 0U;
  }

  void ServiceTx(uint32_t events, bool in_isr, SubmitContext* submit,
                 HardwareOwnerContext* hardware_context) noexcept
  {
    // The current Write() record is the sole synchronous candidate only when no older
    // published metadata precedes it. Pending preload keeps metadata in queue_info_, so
    // this queue-length snapshot remains valid until the current service round commits
    // the head record.
    if (submit != nullptr)
    {
      submit->synchronous_candidate = (port_.queue_info_->Size() == 1U);
    }

    if (HasEvent(events, TxEvent::CONFIG))
    {
      if (backend_.ConfigRequested())
      {
        if (ApplyConfig(in_isr))
        {
          // CONFIG may have consumed a coalesced WRITE or a write may have arrived while
          // the fixed prefix was being drained. Always recheck the authoritative queue.
          (void)service_.Invoke(EventMask(TxEvent::WRITE),
                                [this, in_isr](uint32_t pending_events) noexcept
                                { ServiceTx(pending_events, in_isr, nullptr, nullptr); });
        }
        return;
      }

      // A completion callback may republish CONFIG after another context already
      // finished it. Such a spurious reminder must not absorb unrelated terminal work.
      events &= ~EventMask(TxEvent::CONFIG);
      if (events == 0U)
      {
        return;
      }
    }

    // Once CONFIG fixes its metadata prefix, only CONFIG may advance the model until
    // the backend reports the hardware transaction complete. WRITE and old terminal
    // notifications remain level-triggered reminders; the final mandatory WRITE rescan
    // handles post-boundary submissions, while old terminal events are intentionally
    // discarded because CONFIG retires that hardware generation.
    if (config_boundary_valid_)
    {
      return;
    }

    if (HasEvent(events, TxEvent::ERROR))
    {
      (void)ReleaseActive();
    }
    else if (HasEvent(events, TxEvent::COMPLETE))
    {
      (void)ReleaseActive();
    }

    if (!busy_ && (active_length_ == 0U))
    {
      // Every candidate follows the same path: copy it to pending, then commit it to
      // active only after the backend accepts the DMA start. This also preserves a
      // preloaded candidate when the backend returns RETRY.
      if (!pending_valid_)
      {
        (void)StageNextPending(in_isr);
      }
      if (pending_valid_ && TryStartPending(in_isr, submit, hardware_context))
      {
        return;
      }
    }

    if (busy_)
    {
      (void)StageNextPending(in_isr);
    }
  }

  bool StagePending(const WriteInfoBlock& info)
  {
    if (pending_valid_)
    {
      return false;
    }

    // Move the payload into the UART-owned pending slot. Metadata remains at the queue
    // head until STARTED or FAILED commits this candidate, so queue length still marks
    // whether the current Write() is the only synchronous result candidate.
    const ErrorCode result =
        port_.queue_data_->PopBatch(buffers_.PendingBuffer(), info.data.size_);
    ASSERT(result == ErrorCode::OK);
    if (result != ErrorCode::OK)
    {
      return false;
    }

    pending_valid_ = true;
    return true;
  }

  bool PopInfo(WriteInfoBlock& info)
  {
    const ErrorCode result = port_.queue_info_->Pop(info);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
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

  UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block,
                                  HardwareOwnerContext* hardware_context)
  {
    const auto result = [&]
    {
      if constexpr (BackendSupportsHardwareContext())
      {
        return backend_.StartDmaTx(data, size, block, hardware_context);
      }
      else
      {
        ASSERT(hardware_context == nullptr);
        return backend_.StartDmaTx(data, size, block);
      }
    }();
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

  UartDmaTxStartResult StartCandidate(uint8_t* data, size_t size, int block,
                                      HardwareOwnerContext* hardware_context)
  {
    ASSERT(size > 0U);
    busy_ = true;
    const UartDmaTxStartResult result = StartDmaTx(data, size, block, hardware_context);
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

  bool TryStartPending(bool in_isr, SubmitContext* submit,
                       HardwareOwnerContext* hardware_context)
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

    const bool synchronous_submission =
        (submit != nullptr) && submit->synchronous_candidate;
    const int pending_block = buffers_.ActiveBlock() ^ 1;
    const UartDmaTxStartResult result = StartCandidate(
        buffers_.PendingBuffer(), info.data.size_, pending_block, hardware_context);
    if (result == UartDmaTxStartResult::RETRY)
    {
      return true;
    }

    pending_valid_ = false;
    if (!PopInfo(info))
    {
      ASSERT(false);
      busy_ = false;
      return false;
    }
    if (result == UartDmaTxStartResult::FAILED)
    {
      busy_ = false;
      ClearActive();
      if (synchronous_submission)
      {
        submit->result = ErrorCode::FAILED;
      }
      else
      {
        port_.Finish(in_isr, ErrorCode::FAILED, info);
      }
      // FAILED is a record-level terminal result. The backend has already returned with
      // a reusable, inactive TX path; a hardware recovery request is a separate backend
      // decision and must be published explicitly when needed. Re-scan the authoritative
      // queue because the WRITE snapshot that started this candidate has been consumed.
      (void)service_.Invoke(EventMask(TxEvent::WRITE),
                            [this, in_isr](uint32_t pending_events) noexcept
                            { ServiceTx(pending_events, in_isr, nullptr, nullptr); });
      return true;
    }

    buffers_.FlipActiveBlock();
    active_length_ = info.data.size_;
    if (synchronous_submission)
    {
      submit->result = ErrorCode::OK;
    }
    else
    {
      port_.Finish(in_isr, ErrorCode::OK, info);
    }
    (void)StageNextPending(in_isr);
    return true;
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

    // false keeps the fixed boundary and the backend CONFIG obligation alive for a
    // later ResumeConfig(). true means old hardware activity is quiescent, the latest
    // payload is applied, and RX is restarted; the backend must retain CONFIG hardware
    // ownership until OnConfigApplied() is called after the old TX prefix is drained.
    if (!backend_.ApplyPendingConfig(in_isr))
    {
      return false;
    }

    size_t remaining = config_prefix_count_;
    config_prefix_count_ = 0U;
    config_boundary_valid_ = false;

    (void)ReleaseActive();

    // A preloaded candidate already owns its payload outside queue_data_. Only its
    // metadata remains to be removed from the fixed CONFIG prefix.
    if (pending_valid_)
    {
      ASSERT(remaining > 0U);
      pending_valid_ = false;
      WriteInfoBlock info{};
      if (!PopInfo(info))
      {
        ASSERT(false);
        return false;
      }
      --remaining;
      port_.Finish(in_isr, ErrorCode::FAILED, info);
    }

    FailPublishedQueued(in_isr, remaining);
    (void)backend_.OnConfigApplied(in_isr);
    return true;
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
      const ErrorCode data_result = port_.queue_data_->PopBatch(nullptr, info.data.size_);
      ASSERT(data_result == ErrorCode::OK);
      if (data_result != ErrorCode::OK)
      {
        ASSERT(false);
        return;
      }
      if (!PopInfo(info))
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
  DoubleBufferStorage buffers_;
  SerializedService service_{};
  size_t active_length_ = 0U;
  size_t config_prefix_count_ = 0U;
  bool busy_ = false;
  bool pending_valid_ = false;
  bool config_boundary_valid_ = false;
};

}  // namespace LibXR
