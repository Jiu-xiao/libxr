#pragma once

#include <cstddef>
#include <cstdint>

#include "double_buffer.hpp"
#include "flag.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief Queue-backed one-shot DMA transmit model for UART drivers.
 *
 * The model owns the reusable active/pending transaction state. A platform
 * backend only provides `StartDmaTx(buffer, size, block)`, while DMA completion
 * and error events are returned through `OnTransferDone()`.
 *
 * @tparam Backend Statically bound platform backend.
 * @tparam StateFlag Flag implementation selected for the platform concurrency model.
 */
template <typename Backend, typename StateFlag = Flag::Plain>
class UartDmaTxModel
{
 public:
  /**
   * @brief Bind the model to a backend, write port, and two-half DMA buffer.
   */
  UartDmaTxModel(Backend& backend, WritePort& port, RawData storage)
      : backend_(backend), port_(port), buffers_(storage)
  {
  }

  /**
   * @brief Consume or stage the write request currently queued by `WritePort`.
   *
   * An idle request completes synchronously when DMA starts. A request staged
   * behind active DMA returns `PENDING` and is completed when it is promoted
   * and started by `OnTransferDone()`.
   */
  ErrorCode Submit(bool)
  {
    if (in_completion_.IsSet() || pending_valid_.IsSet())
    {
      return ErrorCode::PENDING;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::PENDING;
    }

    if (info.data.size_ > buffers_.Size())
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    if (busy_.IsSet())
    {
      if (!StagePending(info))
      {
        return ErrorCode::FAILED;
      }

      // DMA may have completed before the pending state was published.
      if (busy_.IsSet())
      {
        return ErrorCode::PENDING;
      }

      if (!PromotePending())
      {
        return ErrorCode::PENDING;
      }

      WriteInfoBlock claimed_info{};
      if (!PopActiveInfo(claimed_info))
      {
        ClearActive();
        return ErrorCode::FAILED;
      }

      return StartActive() ? ErrorCode::OK : FailSynchronousStart();
    }

    WriteInfoBlock claimed_info{};
    if (!PopPayload(buffers_.ActiveBuffer(), info.data.size_) ||
        port_.queue_info_->Pop(claimed_info) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    active_length_ = info.data.size_;
    return StartActive() ? ErrorCode::OK : FailSynchronousStart();
  }

  /**
   * @brief Advance the model after one DMA completion or error event.
   */
  void OnTransferDone(bool in_isr, ErrorCode result)
  {
    if (in_completion_.TestAndSet())
    {
      return;
    }

    if (!busy_.TestAndClear())
    {
      in_completion_.Clear();
      return;
    }

    ClearActive();

    if (result != ErrorCode::OK)
    {
      FailPending(in_isr, result);
      in_completion_.Clear();
      return;
    }

    WriteInfoBlock completed_info{};
    if (!LoadNextActive(completed_info))
    {
      in_completion_.Clear();
      return;
    }

    const bool started = StartActive();
    if (!started)
    {
      ClearActive();
    }
    port_.Finish(in_isr, started ? ErrorCode::OK : ErrorCode::FAILED, completed_info);

    if (started)
    {
      (void)StageNextPending();
    }

    in_completion_.Clear();
  }

  /**
   * @brief Restart the active DMA payload after platform reconfiguration.
   */
  bool RestartActive()
  {
    if (!busy_.IsSet() || (active_length_ == 0U))
    {
      return false;
    }
    return backend_.StartDmaTx(buffers_.ActiveBuffer(), active_length_,
                               buffers_.ActiveBlock());
  }

  /**
   * @brief Return whether DMA currently owns an active request.
   */
  [[nodiscard]] bool IsBusy() const { return busy_.IsSet(); }

  /**
   * @brief Return one fixed DMA buffer block for descriptor initialization.
   */
  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  /**
   * @brief Return the capacity of one DMA buffer block.
   */
  [[nodiscard]] size_t BufferSize() const { return buffers_.Size(); }

 private:
  bool PopPayload(uint8_t* destination, size_t size)
  {
    const ErrorCode result = port_.queue_data_->PopBatch(destination, size);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool StagePending(const WriteInfoBlock& info)
  {
    if (pending_valid_.IsSet() || !PopPayload(buffers_.PendingBuffer(), info.data.size_))
    {
      return false;
    }

    pending_length_ = info.data.size_;
    pending_valid_.Set();
    return true;
  }

  bool StageNextPending()
  {
    if (pending_valid_.IsSet())
    {
      return false;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return false;
    }

    if (info.data.size_ > buffers_.Size())
    {
      ASSERT(false);
      return false;
    }
    return StagePending(info);
  }

  bool PromotePending()
  {
    if (!pending_valid_.TestAndClear())
    {
      return false;
    }

    buffers_.FlipActiveBlock();
    active_length_ = pending_length_;
    pending_length_ = 0U;
    return true;
  }

  bool PopActiveInfo(WriteInfoBlock& info)
  {
    const ErrorCode result = port_.queue_info_->Pop(info);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool LoadNextActive(WriteInfoBlock& info)
  {
    if (PromotePending())
    {
      return PopActiveInfo(info);
    }

    WriteInfoBlock next_info{};
    if (port_.queue_info_->Peek(next_info) != ErrorCode::OK)
    {
      return false;
    }
    if (next_info.data.size_ > buffers_.Size() ||
        !PopPayload(buffers_.ActiveBuffer(), next_info.data.size_) ||
        !PopActiveInfo(info))
    {
      ASSERT(false);
      return false;
    }

    active_length_ = next_info.data.size_;
    return true;
  }

  bool StartActive()
  {
    ASSERT(active_length_ > 0U);
    busy_.Set();
    if (backend_.StartDmaTx(buffers_.ActiveBuffer(), active_length_,
                            buffers_.ActiveBlock()))
    {
      return true;
    }

    busy_.Clear();
    return false;
  }

  ErrorCode FailSynchronousStart()
  {
    ClearActive();
    return ErrorCode::FAILED;
  }

  void FailPending(bool in_isr, ErrorCode result)
  {
    if (!pending_valid_.TestAndClear())
    {
      return;
    }

    pending_length_ = 0U;
    WriteInfoBlock pending_info{};
    if (port_.queue_info_->Pop(pending_info) != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
    port_.Finish(in_isr, result, pending_info);
  }

  void ClearActive() { active_length_ = 0U; }

  Backend& backend_;
  WritePort& port_;
  DoubleBuffer buffers_;
  size_t active_length_ = 0U;
  size_t pending_length_ = 0U;
  StateFlag busy_{};
  StateFlag pending_valid_{};
  StateFlag in_completion_{};
};

}  // namespace LibXR
