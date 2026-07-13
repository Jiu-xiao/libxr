#pragma once

#include <cstddef>
#include <cstdint>

#include "double_buffer.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"
#include "serialized_service.hpp"

namespace LibXR
{

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
  }

  /**
   * @brief 发布 WRITE 事件并推进队首请求 / Publish WRITE and advance the head request
   * @param in_isr 当前调用是否位于 ISR / Whether the current caller is in an ISR
   * @return 当前请求同步启动时返回 `OK`，留在流水中时返回 `PENDING`，同步启动失败
   * 时返回错误 / `OK` for a synchronous start, `PENDING` while retained by the
   * pipeline, or an error for a synchronous start failure
   */
  ErrorCode Submit(bool in_isr)
  {
    SubmitContext context{};
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
      const bool resume_tx = ApplyConfig(in_isr);
      if (!config_boundary_valid_ && resume_tx)
      {
        (void)service_.Invoke(EventMask(TxEvent::WRITE),
                              [this, in_isr](uint32_t pending_events) noexcept
                              { ServiceTx(pending_events, in_isr, nullptr); });
      }
      return;
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

  bool DiscardPayload(size_t size)
  {
    while (size > 0U)
    {
      const size_t chunk = size > buffers_.Size() ? buffers_.Size() : size;
      if (!PopPayload(buffers_.ActiveBuffer(), chunk))
      {
        return false;
      }
      size -= chunk;
    }
    return true;
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

      if (info.data.size_ > buffers_.Size())
      {
        if (!DiscardPayload(info.data.size_) || !PopActiveInfo(info))
        {
          ASSERT(false);
          return false;
        }
        port_.Finish(in_isr, ErrorCode::FAILED, info);
        continue;
      }

      return StagePending(info);
    }
    return false;
  }

  bool PromotePending(WriteInfoBlock& info)
  {
    if (!pending_valid_)
    {
      return false;
    }

    pending_valid_ = false;
    buffers_.FlipActiveBlock();
    if (!PopActiveInfo(info))
    {
      return false;
    }

    active_length_ = info.data.size_;
    return true;
  }

  bool StartActive()
  {
    ASSERT(active_length_ > 0U);
    busy_ = true;
    if (backend_.StartDmaTx(buffers_.ActiveBuffer(), active_length_,
                            buffers_.ActiveBlock()))
    {
      return true;
    }

    busy_ = false;
    return false;
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
    WriteInfoBlock info{};
    if (!PromotePending(info))
    {
      return false;
    }

    const bool started = StartActive();
    if (!started)
    {
      ClearActive();
    }
    port_.Finish(in_isr, started ? ErrorCode::OK : ErrorCode::FAILED, info);

    if (!started)
    {
      RequestConfig(in_isr);
      return true;
    }

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

    if (info.data.size_ > buffers_.Size())
    {
      if (!DiscardPayload(info.data.size_) || !PopActiveInfo(info))
      {
        ASSERT(false);
        return;
      }
      if (submit != nullptr)
      {
        submit->result = ErrorCode::FAILED;
      }
      else
      {
        port_.Finish(in_isr, ErrorCode::FAILED, info);
      }
      return;
    }

    if (!PopPayload(buffers_.ActiveBuffer(), info.data.size_) || !PopActiveInfo(info))
    {
      ASSERT(false);
      if (submit != nullptr)
      {
        submit->result = ErrorCode::FAILED;
      }
      return;
    }

    active_length_ = info.data.size_;
    const bool started = StartActive();
    if (!started)
    {
      ClearActive();
    }

    if (submit != nullptr)
    {
      submit->result = started ? ErrorCode::OK : ErrorCode::FAILED;
    }
    else
    {
      port_.Finish(in_isr, started ? ErrorCode::OK : ErrorCode::FAILED, info);
      if (!started)
      {
        RequestConfig(in_isr);
      }
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
      if (!DiscardPayload(info.data.size_) || !PopActiveInfo(info))
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
