#include "write_port.hpp"

#include <new>

using namespace LibXR;

template class LibXR::SPSCQueue<WriteInfoBlock>;
template class LibXR::SPSCQueue<uint8_t>;

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_info_(new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                      SPSCQueue<WriteInfoBlock>(queue_size)),
      queue_data_(buffer_size > 0 ? new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                                        SPSCQueue<uint8_t>(buffer_size)
                                  : nullptr)
{
}

size_t WritePort::EmptySize()
{
  ASSERT(queue_data_ != nullptr);
  return queue_data_->EmptySize();
}

size_t WritePort::Size()
{
  ASSERT(queue_data_ != nullptr);
  return queue_data_->Size();
}

bool WritePort::Writable() { return write_fun_ != nullptr; }

WritePort& WritePort::operator=(WriteFun fun)
{
  write_fun_ = fun;
  return *this;
}

void WritePort::Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info)
{
  if (info.op.type == WriteOperation::OperationType::BLOCK)
  {
    // Write completion claims the active BLOCK waiter and hands the wakeup to it.
    // 写完成 claim 当前 BLOCK waiter，并把唤醒交给它。
    BusyState state = busy_.load(std::memory_order_acquire);
    while (state == BusyState::OWNER || state == BusyState::BLOCK_WAITING)
    {
      if (busy_.compare_exchange_weak(state, BusyState::BLOCK_CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        block_result_ = ans;
        info.op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
    }

    // The waiter may have timed out and detached before this late completion is
    // reported.
    // waiter 可能已经先超时分离，随后迟到完成才上报。
    ASSERT_FROM_CALLBACK(state == BusyState::BLOCK_DETACHED, in_isr);
    if (state != BusyState::BLOCK_DETACHED)
    {
      return;
    }

    const bool detached = busy_.compare_exchange_strong(
        state, BusyState::IDLE, std::memory_order_acq_rel, std::memory_order_acquire);
    ASSERT_FROM_CALLBACK(detached, in_isr);
    return;
  }

  info.op.UpdateStatus(in_isr, ans);
}

void WritePort::MarkAsRunning(WriteOperation& op) { op.MarkAsRunning(); }

ErrorCode WritePort::operator()(ConstRawData data, WriteOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(op.type != WriteOperation::OperationType::BLOCK || !in_isr,
                        in_isr);

  if (Writable())
  {
    if (data.size_ == 0)
    {
      if (op.type != WriteOperation::OperationType::BLOCK)
      {
        op.UpdateStatus(in_isr, ErrorCode::OK);
      }
      return ErrorCode::OK;
    }

    BusyState expected = BusyState::IDLE;
    if (!busy_.compare_exchange_strong(expected, BusyState::OWNER,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
      return ErrorCode::BUSY;
    }

    return CommitWrite(data, op, false, in_isr);
  }
  else
  {
    return ErrorCode::NOT_SUPPORT;
  }
}

ErrorCode WritePort::CommitWrite(ConstRawData data, WriteOperation& op, bool meta_pushed,
                                 bool in_isr)
{
  if (!meta_pushed && queue_info_->EmptySize() < 1)
  {
    busy_.store(BusyState::IDLE, std::memory_order_release);
    return ErrorCode::FULL;
  }

  ErrorCode ans = ErrorCode::OK;
  if (!meta_pushed)
  {
    if (queue_data_->EmptySize() < data.size_)
    {
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return ErrorCode::FULL;
    }

    ans =
        queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
    UNUSED(ans);
    ASSERT(ans == ErrorCode::OK);

    // Operation state is part of the published queue record. Arm it before metadata
    // becomes visible to a concurrent backend/configuration owner.
    // operation 状态属于已发布队列记录的一部分；在 metadata 对并发 backend/config owner
    // 可见前先挂起。
    op.MarkAsRunning();
    WriteInfoBlock info{data, op};
    ans = queue_info_->Push(info);

    ASSERT(ans == ErrorCode::OK);
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    BusyState expected = BusyState::OWNER;
    if (!busy_.compare_exchange_strong(expected, BusyState::BLOCK_WAITING,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
      REQUIRE(expected == BusyState::BLOCK_CLAIMED);
    }
  }

  ans = write_fun_(*this, in_isr);

  if (ans != ErrorCode::PENDING)
  {
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      const auto state = busy_.load(std::memory_order_acquire);

      if (state == BusyState::BLOCK_CLAIMED)
      {
        auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
        REQUIRE(finish_wait_ans == ErrorCode::OK);
        const ErrorCode result = block_result_;
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return result;
      }

      REQUIRE(state == BusyState::BLOCK_WAITING);
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return ans;
    }

    if (!meta_pushed)
    {
      busy_.store(BusyState::IDLE, std::memory_order_release);
    }

    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return (static_cast<int8_t>(ans) < 0) ? ans : ErrorCode::OK;
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    ASSERT(!in_isr);
    auto wait_ans = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    if (wait_ans == ErrorCode::OK)
    {
      // BLOCK_CLAIMED is always released by the waiter itself.
      // BLOCK_CLAIMED 始终由 waiter 自己释放。
#ifdef LIBXR_DEBUG_BUILD
      auto state = busy_.load(std::memory_order_acquire);
      ASSERT(state == BusyState::BLOCK_CLAIMED);
#endif
      const ErrorCode result = block_result_;
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return result;
    }

    // Timeout won before completion claimed the waiter.
    // 超时先赢，完成侧还没 claim 当前 waiter。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_DETACHED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return ErrorCode::TIMEOUT;
    }

    REQUIRE(expected == BusyState::BLOCK_CLAIMED);

    // Timeout lost after completion had already claimed the waiter.
    // 超时发生得太晚，完成侧已经 claim 了当前 waiter。
    auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
    REQUIRE(finish_wait_ans == ErrorCode::OK);
    const ErrorCode result = block_result_;
    busy_.store(BusyState::IDLE, std::memory_order_release);
    return result;
  }

  if (!meta_pushed)
  {
    busy_.store(BusyState::IDLE, std::memory_order_release);
  }

  return ErrorCode::OK;
}

void WritePort::FailPendingAndResetForBackendTeardown(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);
  WriteInfoBlock info{};
  WriteInfoBlock block_info{};
  bool has_block_info = false;

  const auto drain_queued_records = [&]()
  {
    queue_data_->Reset();
    while (queue_info_->Pop(info) == ErrorCode::OK)
    {
      if (info.op.type == WriteOperation::OperationType::BLOCK)
      {
        DEV_ASSERT_FROM_CALLBACK(!has_block_info, in_isr);
        if (!has_block_info)
        {
          block_info = info;
          has_block_info = true;
        }
        continue;
      }

      Finish(in_isr, reason, info);
    }
  };

  while (true)
  {
    auto state = busy_.load(std::memory_order_acquire);

    if (state == BusyState::OWNER)
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    if (state == BusyState::IDLE)
    {
      BusyState expected = BusyState::IDLE;
      if (!busy_.compare_exchange_strong(expected, BusyState::OWNER,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        continue;
      }

      drain_queued_records();
      DEV_ASSERT_FROM_CALLBACK(!has_block_info, in_isr);
      block_result_ = ErrorCode::OK;
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return;
    }

    if (state == BusyState::BLOCK_WAITING)
    {
      // Keep BLOCK_WAITING visible until Finish() hands the terminal wakeup to
      // the blocked caller. Switching to OWNER here would break that
      // existing waiter handoff.
      // 这里必须保留 BLOCK_WAITING，直到 Finish() 把最终唤醒交给当前
      // BLOCK waiter；若先切成 OWNER，会破坏既有 waiter 交接。
      drain_queued_records();
      DEV_ASSERT_FROM_CALLBACK(has_block_info, in_isr);
      if (has_block_info)
      {
        // Finish publishes the waiter result and may let that caller submit again.
        // Nothing in this drain may touch port state or queues after that release.
        Finish(in_isr, reason, block_info);
      }
      return;
    }

    if (state == BusyState::BLOCK_DETACHED)
    {
      // The waiter is already gone, but BLOCK_DETACHED still blocks reentrant
      // submissions while old queue entries are drained.
      // waiter 已经离开，但 BLOCK_DETACHED 仍能在清理旧队列期间挡住重入提交。
      drain_queued_records();
      if (has_block_info)
      {
        // Finish releases BLOCK_DETACHED. It must be the final drain action because a
        // new submission may acquire the port immediately afterward.
        Finish(in_isr, reason, block_info);
        return;
      }

      BusyState expected = BusyState::BLOCK_DETACHED;
      const bool released = busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_acquire);
      ASSERT_FROM_CALLBACK(released, in_isr);
      return;
    }

    if (state == BusyState::BLOCK_CLAIMED)
    {
      return;
    }
  }
}
