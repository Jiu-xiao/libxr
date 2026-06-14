#pragma once

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief WritePort（info 队列 + data 队列）的“单 op 不跨界”出队辅助器。
 * @brief Dequeue helper for WritePort info/data queues without crossing one operation.
 */
class CDCUartTxOpDequeueHelper final
{
 public:
  /**
   * @brief 构造函数。
   * @brief Constructor.
   * @param port 写端口引用。 Write port reference.
   */
  explicit CDCUartTxOpDequeueHelper(WritePort& port) : port_(port) {}

  /**
   * @brief 重置内部状态。
   * @brief Reset the cached head and byte offset.
   */
  void Reset()
  {
    head_valid_ = false;
    offset_ = 0;
  }

  /**
   * @brief 判断是否存在待发送 op。
   * @brief Check whether a cached or queued transmit operation exists.
   * @return true 存在可处理 op。 A pending operation exists.
   * @return false 不存在可处理 op。 No pending operation exists.
   */
  bool HasOp() { return head_valid_ || (port_.queue_info_->Size() > 0); }

  /**
   * @brief 从当前 op 搬运数据到目标缓冲区，但不跨越 op 边界。
   * @brief Copy bytes from the current operation without crossing its boundary.
   * @param dst 目标缓冲区。 Destination buffer.
   * @param cap 目标缓冲区容量。 Destination capacity in bytes.
   * @param out_len 实际搬运字节数。 Number of bytes copied out.
   * @return `OK` 表示当前 op 已全部取出；`PENDING` 表示当前 op 仍有剩余数据。
   * @return `OK` when the operation is fully drained; `PENDING` when bytes remain.
   */
  ErrorCode Take(uint8_t* dst, std::size_t cap, std::size_t& out_len)
  {
    auto ec = EnsureHead();
    if (ec != ErrorCode::OK)
    {
      out_len = 0;
      return ec;
    }

    const std::size_t REMAINING = Remaining();
    if (REMAINING == 0)
    {
      out_len = 0;
      return ErrorCode::FAILED;
    }

    const std::size_t TAKE = (REMAINING < cap) ? REMAINING : cap;

    if (port_.queue_data_->PopBatch(dst, TAKE) != ErrorCode::OK)
    {
      out_len = 0;
      return ErrorCode::FAILED;
    }

    offset_ += TAKE;
    out_len = TAKE;

    return (offset_ == head_.data.size_) ? ErrorCode::OK : ErrorCode::PENDING;
  }

  /**
   * @brief 判断缓存的 head op 是否已经完全出队。
   * @brief Check whether the cached head operation has been fully dequeued.
   */
  bool HeadCompleted() const { return head_valid_ && (offset_ == head_.data.size_); }

  /**
   * @brief 当前 head op 完成后弹出对应 info。
   * @brief Pop the metadata after the current head operation is complete.
   * @param completed_info 可选输出被弹出的 info。 Optional output for popped metadata.
   * @return 成功返回 `OK`，head 未完成返回 `FAILED`。
   * @return `OK` on success, or `FAILED` when the head operation is not complete.
   */
  ErrorCode PopCompleted(WriteInfoBlock* completed_info = nullptr)
  {
    if (!HeadCompleted())
    {
      return ErrorCode::FAILED;
    }

    WriteInfoBlock popped{};
    auto ans = port_.queue_info_->Pop(popped);
    ASSERT(ans == ErrorCode::OK);

    if (completed_info)
    {
      *completed_info = popped;
    }

    Reset();
    return ErrorCode::OK;
  }

  /**
   * @brief 丢弃当前 head op 的剩余数据并弹出对应 info。
   * @brief Drop remaining bytes from the current head operation and pop its metadata.
   * @param dropped_info 可选输出被弹出的 info。 Optional output for popped metadata.
   * @return 成功返回 `OK`，队列为空返回 `EMPTY`，数据丢弃失败返回对应错误码。
   * @return `OK` on success, `EMPTY` when no operation exists, or the data-drop error.
   */
  ErrorCode DropHead(WriteInfoBlock* dropped_info = nullptr)
  {
    auto ec = EnsureHead();
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    const std::size_t REMAINING = Remaining();
    if (REMAINING > 0)
    {
      auto drop_ans = port_.queue_data_->PopBatch(nullptr, REMAINING);
      if (drop_ans != ErrorCode::OK)
      {
        return drop_ans;
      }
    }

    WriteInfoBlock popped{};
    auto pop_ans = port_.queue_info_->Pop(popped);
    ASSERT(pop_ans == ErrorCode::OK);
    if (pop_ans != ErrorCode::OK)
    {
      return pop_ans;
    }

    if (dropped_info)
    {
      *dropped_info = popped;
    }

    Reset();
    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 确保当前 head info 已缓存。
   * @brief Ensure that the current head metadata is cached.
   * @return 成功返回 `OK`，info 队列为空返回 `EMPTY`。
   * @return `OK` on success, or `EMPTY` when the info queue is empty.
   */
  ErrorCode EnsureHead()
  {
    if (head_valid_)
    {
      return ErrorCode::OK;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    head_ = info;
    head_valid_ = true;
    offset_ = 0;
    return ErrorCode::OK;
  }

  /**
   * @brief 获取当前 op 剩余未出队字节数。
   * @brief Return the remaining byte count of the current operation.
   */
  std::size_t Remaining() const
  {
    ASSERT(head_valid_);
    ASSERT(head_.data.size_ >= offset_);
    return head_.data.size_ - offset_;
  }

  WritePort& port_;          ///< 写端口引用。 Write port reference.
  bool head_valid_ = false;  ///< head 缓存有效标志。 Cached head valid flag.
  WriteInfoBlock head_{};    ///< 缓存的 head info。 Cached head metadata.
  std::size_t offset_ = 0;   ///< 当前 op 已出队偏移。 Dequeued offset in current op.
};

}  // namespace LibXR
