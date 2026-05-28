#include "write_port.hpp"

#include <new>

using namespace LibXR;

template class LibXR::LockFreeQueue<WriteInfoBlock>;
template class LibXR::LockFreeQueue<uint8_t>;

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_info_(new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
                      LockFreeQueue<WriteInfoBlock>(queue_size)),
      queue_data_(buffer_size > 0 ? new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
                                        LockFreeQueue<uint8_t>(buffer_size)
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
    block_result_ = ans;

    // Write completion claims the active BLOCK waiter and hands the wakeup to it.
    // 写完成 claim 当前 BLOCK waiter，并把唤醒交给它。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    // The waiter may have timed out and detached before this late completion is
    // reported.
    // waiter 可能已经先超时分离，随后迟到完成才上报。
    if (expected == BusyState::BLOCK_PUBLISHING)
    {
      expected = BusyState::BLOCK_PUBLISHING;
      if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        info.op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
    }

    ASSERT(expected == BusyState::BLOCK_DETACHED || expected == BusyState::IDLE ||
           expected == BusyState::LOCKED || expected == BusyState::RESETTING);
    if (expected == BusyState::BLOCK_DETACHED)
    {
      expected = BusyState::BLOCK_DETACHED;
      (void)busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire);
    }
    return;
  }

  info.op.UpdateStatus(in_isr, ans);
}

void WritePort::MarkAsRunning(WriteOperation& op) { op.MarkAsRunning(); }

ErrorCode WritePort::operator()(ConstRawData data, WriteOperation& op, bool in_isr)
{
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
    if (!busy_.compare_exchange_strong(expected, BusyState::LOCKED,
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

    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      // The BLOCK waiter must be armed before queue_info_ becomes visible to a
      // backend/completion thread.
      // queue_info_ 对后端/完成线程可见前，必须先挂起 BLOCK waiter。
      op.MarkAsRunning();
      busy_.store(BusyState::BLOCK_PUBLISHING, std::memory_order_release);
    }

    WriteInfoBlock info{data, op};
    ans = queue_info_->Push(info);

    ASSERT(ans == ErrorCode::OK);
  }

  if (op.type != WriteOperation::OperationType::BLOCK)
  {
    op.MarkAsRunning();
  }
  else if (meta_pushed)
  {
    op.MarkAsRunning();
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    BusyState expected = BusyState::BLOCK_PUBLISHING;
    if (!busy_.compare_exchange_strong(expected, BusyState::BLOCK_WAITING,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
      ASSERT(expected == BusyState::BLOCK_CLAIMED);
    }
  }

  ans = write_fun_(*this, in_isr);

  if (ans != ErrorCode::PENDING)
  {
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      auto state = busy_.load(std::memory_order_acquire);
      while (state == BusyState::RESETTING)
      {
        state = busy_.load(std::memory_order_acquire);
      }

      if (state == BusyState::BLOCK_CLAIMED)
      {
        auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
        UNUSED(finish_wait_ans);
        ASSERT(finish_wait_ans == ErrorCode::OK);
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return block_result_;
      }

      if (state == BusyState::BLOCK_DETACHED)
      {
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return ErrorCode::TIMEOUT;
      }

      ASSERT(state == BusyState::BLOCK_WAITING || state == BusyState::IDLE ||
             state == BusyState::LOCKED);
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
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return block_result_;
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

    if (expected != BusyState::BLOCK_CLAIMED)
    {
      while (expected == BusyState::RESETTING)
      {
        expected = busy_.load(std::memory_order_acquire);
      }

      // A detached late completion may already have cleared BLOCK_DETACHED
      // back to IDLE before this waiter wakes from timeout.
      // 分离后的迟到完成可能会在当前 waiter 超时醒来前，先把
      // BLOCK_DETACHED 清回 IDLE。
      ASSERT(expected == BusyState::BLOCK_DETACHED || expected == BusyState::IDLE ||
             expected == BusyState::LOCKED);
      if (expected == BusyState::BLOCK_DETACHED)
      {
        busy_.store(BusyState::IDLE, std::memory_order_release);
      }
      return ErrorCode::TIMEOUT;
    }

    // Timeout lost after completion had already claimed the waiter.
    // 超时发生得太晚，完成侧已经 claim 了当前 waiter。
    auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
    UNUSED(finish_wait_ans);
    ASSERT(finish_wait_ans == ErrorCode::OK);
    busy_.store(BusyState::IDLE, std::memory_order_release);

    return block_result_;
  }

  if (!meta_pushed)
  {
    busy_.store(BusyState::IDLE, std::memory_order_release);
  }

  return ErrorCode::OK;
}

void WritePort::FailAndClearAll(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);
  WriteInfoBlock info{};

  while (true)
  {
    auto state = busy_.load(std::memory_order_acquire);

    if (state == BusyState::LOCKED)
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    if (state == BusyState::BLOCK_PUBLISHING)
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    if (state == BusyState::RESETTING)
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    if (state == BusyState::IDLE)
    {
      BusyState expected = BusyState::IDLE;
      if (!busy_.compare_exchange_strong(expected, BusyState::RESETTING,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        continue;
      }

      queue_data_->Reset();
      while (queue_info_->Pop(info) == ErrorCode::OK)
      {
        Finish(in_isr, reason, info);
      }
      block_result_ = ErrorCode::OK;
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return;
    }

    if (state == BusyState::BLOCK_WAITING)
    {
      // Keep BLOCK_WAITING visible until Finish() hands the terminal wakeup to
      // the blocked caller. Switching to RESETTING here would break that
      // existing waiter handoff.
      // 这里必须保留 BLOCK_WAITING，直到 Finish() 把最终唤醒交给当前
      // BLOCK waiter；若先切成 RESETTING，会破坏既有 waiter 交接。
      queue_data_->Reset();
      while (queue_info_->Pop(info) == ErrorCode::OK)
      {
        Finish(in_isr, reason, info);
      }
      return;
    }

    if (state == BusyState::BLOCK_DETACHED)
    {
      // The waiter is already gone, but BLOCK_DETACHED still blocks reentrant
      // submissions while old queue entries are drained.
      // waiter 已经离开，但 BLOCK_DETACHED 仍能在清理旧队列期间挡住重入提交。
      queue_data_->Reset();
      while (queue_info_->Pop(info) == ErrorCode::OK)
      {
        // The waiter has already detached. Finish() will clear the local state
        // without re-posting that waiter.
        // waiter 已经分离。Finish() 会清理本地状态，但不会重新唤醒该 waiter。
        Finish(in_isr, reason, info);
      }
      block_result_ = ErrorCode::OK;
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return;
    }

    if (state == BusyState::BLOCK_CLAIMED)
    {
      return;
    }
  }
}
