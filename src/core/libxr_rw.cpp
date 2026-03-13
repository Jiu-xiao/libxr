#include "libxr_rw.hpp"

#include "libxr_def.hpp"
#include "mutex.hpp"

using namespace LibXR;

template class LibXR::LockFreeQueue<WriteInfoBlock>;
template class LibXR::LockFreeQueue<uint8_t>;

ReadPort::ReadPort(size_t buffer_size)
    : queue_data_(buffer_size > 0 ? new (std::align_val_t(LIBXR_CACHE_LINE_SIZE))
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
    block_result_ = ans;

    // BLOCK read completion can be claimed in two places:
    // 1. Finish() directly claims PENDING -> BLOCK_CLAIMED for driver-led completion.
    // 2. ProcessPendingReads() pre-claims PENDING -> BLOCK_CLAIMED before copying
    //    queued bytes into the caller buffer.
    // BLOCK 读有两个 claim 点：
    // 1. 驱动直接完成时由 Finish() 抢占 PENDING -> BLOCK_CLAIMED。
    // 2. 软件队列补数时由 ProcessPendingReads() 先 claim，再把数据拷到调用者缓冲区。
    BusyState expected = BusyState::PENDING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    if (expected == BusyState::BLOCK_CLAIMED)
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    // Timeout/reset already detached this waiter. Best-effort clear the detach
    // marker back to IDLE; this completion must not post anymore because the
    // caller has already returned.
    // 超时或 Reset 已把等待者分离；这里只做尽力清理，绝不能再 Post，
    // 因为调用者已经从超时路径返回了。
    expected = BusyState::BLOCK_DETACHED;
    (void)busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire);
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
        }

        OnRxDequeue(in_isr);

        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(in_isr, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }

      info_ = ReadInfoBlock{data, op};

      op.MarkAsRunning();

      auto ans = read_fun_(*this, in_isr);

      if (ans == ErrorCode::PENDING)
      {
        BusyState expected = BusyState::IDLE;
        if (busy_.compare_exchange_weak(expected, BusyState::PENDING,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
        {
          break;
        }
        else
        {
          continue;
        }
      }
      else
      {
        if (op.type == ReadOperation::OperationType::BLOCK)
        {
          return ans;
        }
        op.UpdateStatus(in_isr, ans);
        return ErrorCode::OK;
      }
    }

    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      ASSERT(!in_isr);
      auto wait_ans = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      if (wait_ans == ErrorCode::OK)
      {
        // The final wakeup now belongs to this waiter. Finish() never releases
        // BLOCK_CLAIMED -> IDLE on behalf of the caller; the waiter does it here.
        // 最终唤醒已经归当前 waiter 所有；Finish() 不替调用者释放
        // BLOCK_CLAIMED -> IDLE，这一步必须由 waiter 自己完成。
#ifdef LIBXR_DEBUG_BUILD
        auto state = busy_.load(std::memory_order_acquire);
        ASSERT(state == BusyState::BLOCK_CLAIMED);
#endif
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return block_result_;
      }

      // Timeout won cleanly: the driver had not claimed the completion yet, so
      // we can detach and return TIMEOUT immediately.
      // 超时路径干净获胜：底层还没 claim 完成，所以可以直接脱离并返回 TIMEOUT。
      BusyState expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }

      // compare_exchange_strong() already wrote the current state back into
      // `expected` on failure, so we can branch on that exact post-race state
      // without taking another atomic load.
      // compare_exchange_strong() 失败时已经把当前状态回填到 `expected`，
      // 所以这里直接按 race 之后的真实状态分流，不再额外 load 一次。
      switch (expected)
      {
        case BusyState::BLOCK_CLAIMED:
          break;
        case BusyState::BLOCK_DETACHED:
          busy_.store(BusyState::IDLE, std::memory_order_release);
          return ErrorCode::TIMEOUT;
        default:
          ASSERT(false);
          return ErrorCode::TIMEOUT;
      }

      // Timeout lost after completion had already claimed the waiter. Drain the
      // final post here so no semaphore token leaks into the next BLOCK op.
      // 超时发生得太晚，完成侧已经 claim 了本 waiter；这里把最终 Post 吃掉，
      // 避免信号量令牌泄露到下一次 BLOCK 操作。
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

  auto is_busy = busy_.load(std::memory_order_relaxed);

  if (is_busy == BusyState::PENDING)
  {
    auto size = queue_data_->Size();
    if (size > 0 && size >= info_.data.size_)
    {
      if (info_.op.type == ReadOperation::OperationType::BLOCK)
      {
        // Queue-fed completion must claim the BLOCK waiter before copying bytes,
        // otherwise timeout/reset could race and leave the waiter ownership
        // ambiguous.
        // 软件队列喂数时，必须先 claim BLOCK waiter 再拷数据；
        // 否则 timeout/reset 可能并发抢占，导致 waiter 所有权不清楚。
        BusyState expected = BusyState::PENDING;
        if (!busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire))
        {
          return;
        }
      }

      if (info_.data.size_ > 0)
      {
        auto ans = queue_data_->PopBatch(reinterpret_cast<uint8_t*>(info_.data.addr_),
                                         info_.data.size_);
        UNUSED(ans);
        ASSERT(ans == ErrorCode::OK);
      }

      Finish(in_isr, ErrorCode::OK, info_);
      OnRxDequeue(in_isr);
    }
  }
  else if (is_busy == BusyState::IDLE)
  {
    busy_.store(BusyState::EVENT, std::memory_order_release);
  }
}

void ReadPort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  queue_data_->Reset();

  auto state = busy_.load(std::memory_order_acquire);
  if (state == BusyState::PENDING && info_.op.type == ReadOperation::OperationType::BLOCK)
  {
    // Reset does not reopen the port immediately if a BLOCK waiter is still
    // armed. It first detaches the waiter, and the waiter/late completion pair
    // is responsible for draining that detached state.
    // Reset 不会在 BLOCK waiter 还挂着时立刻把端口重开；它先把 waiter
    // 分离，后续由 waiter/迟到完成这对并发方自己把分离状态排干净。
    BusyState expected = BusyState::PENDING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_DETACHED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return;
    }

    state = busy_.load(std::memory_order_acquire);
  }

  if (state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_DETACHED)
  {
    return;
  }

  busy_.store(BusyState::IDLE, std::memory_order_release);
  block_result_ = ErrorCode::OK;
}

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_info_(new (std::align_val_t(LIBXR_CACHE_LINE_SIZE))
                      LockFreeQueue<WriteInfoBlock>(queue_size)),
      queue_data_(buffer_size > 0 ? new (std::align_val_t(LIBXR_CACHE_LINE_SIZE))
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

    // Write-side BLOCK completion has only one normal claim site:
    // Finish() moves BLOCK_WAITING -> BLOCK_CLAIMED, then hands the final post
    // to the waiting thread.
    // 写侧 BLOCK 完成只有一个正常 claim 点：
    // Finish() 把 BLOCK_WAITING -> BLOCK_CLAIMED，然后把最终 Post 交给等待线程。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    // If timeout/reset already detached the waiter, this completion is only
    // allowed to clear BLOCK_DETACHED back to IDLE. Any other state means the
    // wakeup ownership is no longer ours, so we must stay silent.
    // 如果 timeout/reset 已经把 waiter 分离，这个完成侧最多只允许把
    // BLOCK_DETACHED 清回 IDLE；其他状态都说明唤醒所有权已经不在这里了，
    // 不能再 Post。
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

    WriteInfoBlock info{data, op};
    ans = queue_info_->Push(info);

    ASSERT(ans == ErrorCode::OK);
  }

  op.MarkAsRunning();

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    // Arm the waiter before the driver can complete the write immediately.
    // BLOCK waiter must be armed before write_fun_() runs, otherwise a very
    // fast completion could finish and post before the waiter state exists.
    // 必须先把 BLOCK waiter 挂起来再调用 write_fun_()，否则极快完成可能会在
    // waiter 状态建立前就结束并 Post。
    busy_.store(BusyState::BLOCK_WAITING, std::memory_order_release);
  }

  ans = write_fun_(*this, in_isr);

  if (ans != ErrorCode::PENDING)
  {
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
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
    return ErrorCode::OK;
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    ASSERT(!in_isr);
    auto wait_ans = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    if (wait_ans == ErrorCode::OK)
    {
      // The final wakeup now belongs to this waiter; release the claimed state
      // here instead of from the completion side.
      // 最终唤醒已经归当前 waiter 所有；由 waiter 自己把 claim 状态释放掉。
#ifdef LIBXR_DEBUG_BUILD
      auto state = busy_.load(std::memory_order_acquire);
      ASSERT(state == BusyState::BLOCK_CLAIMED);
#endif
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return block_result_;
    }

    // Timeout won cleanly: write completion had not claimed the waiter yet.
    // 超时路径干净获胜：写完成侧还没来得及 claim 当前 waiter。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_DETACHED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return ErrorCode::TIMEOUT;
    }

    // compare_exchange_strong() already wrote the current state back into
    // `expected` on failure, so we can branch on that exact post-race state
    // without taking another atomic load.
    // compare_exchange_strong() 失败时已经把当前状态回填到 `expected`，
    // 所以这里直接按 race 之后的真实状态分流，不再额外 load 一次。
    switch (expected)
    {
      case BusyState::BLOCK_CLAIMED:
        break;
      case BusyState::BLOCK_DETACHED:
        busy_.store(BusyState::IDLE, std::memory_order_release);
        return ErrorCode::TIMEOUT;
      default:
        ASSERT(false);
        return ErrorCode::TIMEOUT;
    }

    // Timeout lost after completion had already claimed the waiter. Drain the
    // final post here so no semaphore token survives into the next BLOCK write.
    // 超时发生得太晚，完成侧已经 claim 了当前 waiter；这里把最终 Post 吃掉，
    // 避免信号量令牌残留到下一次 BLOCK 写。
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

void WritePort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  queue_data_->Reset();
  queue_info_->Reset();

  auto state = busy_.load(std::memory_order_acquire);
  if (state == BusyState::BLOCK_WAITING)
  {
    // Reset detaches the waiter first; it does not directly release a waiting
    // BLOCK write back to IDLE because the old waiter may still be draining.
    // Reset 先把 waiter 分离，而不是直接把等待中的 BLOCK 写释放回 IDLE，
    // 因为旧 waiter 可能还在收尾。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_DETACHED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return;
    }

    state = busy_.load(std::memory_order_acquire);
  }

  if (state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_DETACHED)
  {
    return;
  }

  busy_.store(BusyState::IDLE, std::memory_order_release);
  block_result_ = ErrorCode::OK;
}

WritePort::Stream::Stream(LibXR::WritePort* port, LibXR::WriteOperation op)
    : port_(port), op_(op)
{
  BusyState expected = BusyState::IDLE;
  if (!port_->busy_.compare_exchange_strong(expected, BusyState::LOCKED,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
  {
    return;
  }

  if (port_->queue_info_->EmptySize() < 1)
  {
    port_->busy_.store(BusyState::IDLE, std::memory_order_release);
    return;
  }

  locked_ = true;
  cap_ = port_->queue_data_->EmptySize();
}

WritePort::Stream::~Stream()
{
  if (locked_ && size_ > 0)
  {
    auto ans =
        port_->queue_info_->Push(WriteInfoBlock{ConstRawData{nullptr, size_}, op_});
    ASSERT(ans == ErrorCode::OK);
    port_->CommitWrite({nullptr, size_}, op_, true);
    if (op_.type == WriteOperation::OperationType::BLOCK)
    {
      // WritePort now owns the BLOCK wait/finish state machine.
      locked_ = false;
    }
  }

  if (locked_)
  {
    port_->busy_.store(BusyState::IDLE, std::memory_order_release);
  }
}

WritePort::Stream& WritePort::Stream::operator<<(const ConstRawData& data)
{
  if (!locked_)
  {
    BusyState expected = BusyState::IDLE;
    if (!port_->busy_.compare_exchange_strong(expected, BusyState::LOCKED,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
    {
      return *this;
    }

    if (port_->queue_info_->EmptySize() < 1)
    {
      port_->busy_.store(BusyState::IDLE, std::memory_order_release);
      return *this;
    }

    locked_ = true;
    cap_ = port_->queue_data_->EmptySize();
  }
  if (size_ + data.size_ <= cap_)
  {
    auto ans = port_->queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                             data.size_);
    ASSERT(ans == ErrorCode::OK);
    size_ += data.size_;
  }

  return *this;
}

ErrorCode WritePort::Stream::Commit()
{
  auto ans = ErrorCode::OK;

  if (locked_ && size_ > 0)
  {
    ans = port_->queue_info_->Push(WriteInfoBlock{ConstRawData{nullptr, size_}, op_});
    ASSERT(ans == ErrorCode::OK);
    ans = port_->CommitWrite({nullptr, size_}, op_, true);
    if (op_.type == WriteOperation::OperationType::BLOCK)
    {
      // WritePort will release busy_ after the BLOCK handoff completes.
      size_ = 0;
      locked_ = false;
      return ans;
    }

    ASSERT(ans == ErrorCode::OK);
    size_ = 0;
  }

  if (locked_)
  {
    locked_ = false;
    port_->busy_.store(BusyState::IDLE, std::memory_order_release);
  }

  return ans;
}

// NOLINTNEXTLINE
int STDIO::Printf(const char* fmt, ...)
{
#if LIBXR_PRINTF_BUFFER_SIZE > 0
  if (!STDIO::write_ || !STDIO::write_->Writable())
  {
    return -1;
  }

  if (!write_mutex_)
  {
    write_mutex_ = new LibXR::Mutex();
  }

  LibXR::Mutex::LockGuard lock_guard(*write_mutex_);

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(STDIO::printf_buff_, LIBXR_PRINTF_BUFFER_SIZE, fmt, args);
  va_end(args);

  // Check result and limit length
  if (len < 0)
  {
    return -1;
  }
  if (static_cast<size_t>(len) >= LIBXR_PRINTF_BUFFER_SIZE)
  {
    len = LIBXR_PRINTF_BUFFER_SIZE - 1;
  }

  ConstRawData data = {reinterpret_cast<const uint8_t*>(STDIO::printf_buff_),
                       static_cast<size_t>(len)};

  static WriteOperation op;  // NOLINT
  auto ans = ErrorCode::OK;
  if (write_stream_ == nullptr)
  {
    ans = STDIO::write_->operator()(data, op);
  }
  else
  {
    (*write_stream_) << data;
    ans = write_stream_->Commit();
  }

  if (ans == ErrorCode::OK)
  {
    return len;
  }
  else
  {
    return -1;
  }

#else
  UNUSED(fmt);
  return 0;
#endif
}
