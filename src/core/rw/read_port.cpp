#include "read_port.hpp"

#include <new>

using namespace LibXR;

ReadPort::ReadPort(size_t buffer_size)
    : queue_data_(buffer_size > 0 ? new (std::align_val_t(LibXR::CACHE_LINE_SIZE))
                                        LockFreeQueue<uint8_t>(buffer_size)
                                  : nullptr)
{
}

size_t ReadPort::EmptySize()
{
  ASSERT(queue_data_ != nullptr);
  return queue_data_->EmptySize();
}

size_t ReadPort::Size()
{
  ASSERT(queue_data_ != nullptr);
  return queue_data_->Size();
}

bool ReadPort::Readable() { return read_fun_ != nullptr; }

ReadPort& ReadPort::operator=(ReadFun fun)
{
  read_fun_ = fun;
  return *this;
}

void ReadPort::Finish(bool in_isr, ErrorCode ans, ReadInfoBlock& info)
{
  if (info.op.type == ReadOperation::OperationType::BLOCK)
  {
    // Read completion is queue-driven. ProcessPendingReads() must claim the
    // BLOCK waiter before data is copied and Finish() is called.
    // 读完成只走队列路径。ProcessPendingReads() 必须先 claim BLOCK waiter，
    // 再拷贝数据并调用 Finish()。
    ASSERT(busy_.load(std::memory_order_acquire) == BusyState::BLOCK_CLAIMED);
    block_result_ = ans;
    info.op.data.sem_info.sem->PostFromCallback(in_isr);
    return;
  }

  busy_.store(BusyState::IDLE, std::memory_order_release);
  info.op.UpdateStatus(in_isr, ans);
}

void ReadPort::MarkAsRunning(ReadInfoBlock& info) { info.op.MarkAsRunning(); }

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op, bool in_isr)
{
  if (Readable())
  {
    BusyState is_busy = busy_.load(std::memory_order_acquire);

    if (is_busy != BusyState::IDLE && is_busy != BusyState::EVENT)
    {
      return ErrorCode::BUSY;
    }

    while (true)
    {
      busy_.store(BusyState::IDLE, std::memory_order_release);

      auto readable_size = queue_data_->Size();

      if (readable_size >= data.size_ && readable_size != 0)
      {
        if (data.size_ > 0)
        {
          auto ans =
              queue_data_->PopBatch(reinterpret_cast<uint8_t*>(data.addr_), data.size_);
          ASSERT(ans == ErrorCode::OK);
          OnRxDequeue(in_isr);
        }

        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(in_isr, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }

      info_ = ReadInfoBlock{data, op};

      op.MarkAsRunning();

      BusyState expected = BusyState::IDLE;
      if (!busy_.compare_exchange_strong(expected, BusyState::PENDING,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        ASSERT(expected == BusyState::EVENT);
        continue;
      }

      auto ans = read_fun_(*this, in_isr);
      if (static_cast<int8_t>(ans) >= 0)
      {
        break;
      }

      // read_fun_ failed while arming/notifying the backend. Roll back only if no
      // producer has completed this pending read concurrently.
      // read_fun_ 挂起/通知底层失败；只有未被 producer 并发完成时，才回滚 pending。
      expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(in_isr, ans);
        }
        return ans;
      }

      if (expected == BusyState::BLOCK_DETACHED)
      {
        return ErrorCode::TIMEOUT;
      }
      if (expected == BusyState::IDLE)
      {
        // A non-BLOCK read may have completed through ProcessPendingReads() before the
        // arm failure returned.
        // 非 BLOCK 读可能在挂起失败返回前，已经通过 ProcessPendingReads() 完成。
        ASSERT(op.type != ReadOperation::OperationType::BLOCK);
        return ErrorCode::OK;
      }
      ASSERT(expected == BusyState::BLOCK_CLAIMED);
      break;
    }

    if (op.type == ReadOperation::OperationType::BLOCK)
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

      // BLOCK wait timed out after the backend had accepted the read. Cancel only if
      // completion has not claimed this waiter.
      // 底层已接受读请求后，BLOCK 等待超时；只有完成侧尚未 claim 当前 waiter 时才取消。
      BusyState expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }

      if (expected == BusyState::BLOCK_DETACHED)
      {
        // The waiter had already detached before the timeout-side cancel won.
        // 当前 waiter 已经先分离；超时侧负责把端口收回 IDLE。
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return ErrorCode::TIMEOUT;
      }

      ASSERT(expected == BusyState::BLOCK_CLAIMED);

      // Timeout lost after completion had already claimed the waiter.
      // 超时发生得太晚，完成侧已经 claim 了当前 waiter。
      auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
      UNUSED(finish_wait_ans);
      ASSERT(finish_wait_ans == ErrorCode::OK);
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return block_result_;
    }
    else
    {
      return ErrorCode::OK;
    }
  }
  else
  {
    return ErrorCode::NOT_SUPPORT;
  }
}

void ReadPort::ProcessPendingReads(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    auto is_busy = busy_.load(std::memory_order_acquire);

    if (is_busy == BusyState::PENDING)
    {
      auto size = queue_data_->Size();
      if (size > 0 && size >= info_.data.size_)
      {
        if (info_.op.type == ReadOperation::OperationType::BLOCK)
        {
          // Read BLOCK completion is claimed here before copying data.
          // BLOCK 读完成在这里先 claim，再拷数据。
          BusyState expected = BusyState::PENDING;
          if (!busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire))
          {
            continue;
          }
        }

        if (info_.data.size_ > 0)
        {
          auto ans = queue_data_->PopBatch(reinterpret_cast<uint8_t*>(info_.data.addr_),
                                           info_.data.size_);
          UNUSED(ans);
          ASSERT(ans == ErrorCode::OK);
          Finish(in_isr, ErrorCode::OK, info_);
          OnRxDequeue(in_isr);
        }
        else
        {
          Finish(in_isr, ErrorCode::OK, info_);
        }
      }
      return;
    }

    if (is_busy == BusyState::IDLE)
    {
      // Data arrived before a waiter was armed. This must be a CAS: a reader may
      // publish PENDING after the load above, and EVENT must not overwrite it.
      // 数据先于 waiter 到达。这里必须用 CAS：读线程可能在上面的 load 之后发布
      // PENDING，EVENT 不能覆盖它。
      BusyState expected = BusyState::IDLE;
      if (busy_.compare_exchange_strong(expected, BusyState::EVENT,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        return;
      }
      continue;
    }

    return;
  }
}

ErrorCode ReadPort::ClearQueuedData(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    auto state = busy_.load(std::memory_order_acquire);
    if (state != BusyState::IDLE && state != BusyState::EVENT)
    {
      return ErrorCode::BUSY;
    }

    busy_.store(BusyState::IDLE, std::memory_order_release);

    const size_t queued_size = queue_data_->Size();
    if (queued_size == 0)
    {
      return ErrorCode::OK;
    }

    const ErrorCode pop_ans = queue_data_->PopBatch(nullptr, queued_size);
    if (pop_ans == ErrorCode::OK)
    {
      OnRxDequeue(in_isr);
      return ErrorCode::OK;
    }

    // Without a dedicated busy state, ClearQueuedData() can race with a
    // concurrent ready-read that consumes part or all of the snapshot after
    // the size check. Treat that as a normal concurrent outcome and retry.
    // 在不引入专用 busy 状态的前提下，ClearQueuedData() 可能和并发的
    // ready-read 竞争：对方会在本次 size 快照之后先消费掉部分或全部字节。
    // 这属于正常并发结果，这里重试即可。
    ASSERT(pop_ans == ErrorCode::EMPTY);
  }
}

void ReadPort::FailAndClearAll(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);
  queue_data_->Reset();

  auto state = busy_.load(std::memory_order_acquire);
  if (state == BusyState::PENDING)
  {
    if (info_.op.type == ReadOperation::OperationType::BLOCK)
    {
      // Backend is already unavailable. Claim the waiter and complete it with
      // the requested failure reason instead of leaving it to timeout.
      // 后端已经不可用。这里直接 claim waiter，并用指定错误收口，
      // 而不是让它继续超时等待。
      BusyState expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        Finish(in_isr, reason, info_);
        return;
      }
      state = expected;
    }
    else
    {
      busy_.store(BusyState::IDLE, std::memory_order_release);
      info_.op.UpdateStatus(in_isr, reason);
      return;
    }
  }

  if (state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_DETACHED)
  {
    return;
  }

  block_result_ = ErrorCode::OK;
  busy_.store(BusyState::IDLE, std::memory_order_release);
}
