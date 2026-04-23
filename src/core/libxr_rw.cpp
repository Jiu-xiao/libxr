#include "libxr_rw.hpp"

#include "libxr_def.hpp"
#include "mutex.hpp"

using namespace LibXR;

template class LibXR::LockFreeQueue<WriteInfoBlock>;
template class LibXR::LockFreeQueue<uint8_t>;

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
    // Read completion must come from ProcessPendingReads(); drivers are not
    // allowed to finish a BLOCK read directly.
    // BLOCK 读完成只能来自 ProcessPendingReads()；驱动不能直接 Finish。
    BusyState expected = BusyState::BLOCK_DETACHED;
    if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return;
    }

    ASSERT(expected == BusyState::BLOCK_CLAIMED);
    if (expected == BusyState::BLOCK_CLAIMED)
    {
      block_result_ = ans;
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
    }
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
      BusyState expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }

      if (expected != BusyState::BLOCK_CLAIMED)
      {
        // A detached late completion may already have cleared BLOCK_DETACHED
        // back to IDLE before this waiter wakes from timeout.
        // 分离后的迟到完成可能会在当前 waiter 超时醒来前，先把
        // BLOCK_DETACHED 清回 IDLE。
        ASSERT(expected == BusyState::BLOCK_DETACHED || expected == BusyState::IDLE);
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
        // Read BLOCK completion is claimed here before copying data.
        // BLOCK 读完成在这里先 claim，再拷数据。
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
    // Reset detaches the BLOCK waiter instead of reopening the port directly.
    // Reset 先分离 BLOCK waiter，不直接重开端口。
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

  block_result_ = ErrorCode::OK;
  busy_.store(BusyState::IDLE, std::memory_order_release);
}

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

    // Write completion claims BLOCK_WAITING and hands the wakeup to the waiter.
    // 写完成从 BLOCK_WAITING claim 当前 waiter，并把唤醒交给它。
    BusyState expected = BusyState::BLOCK_WAITING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    ASSERT(expected == BusyState::BLOCK_DETACHED);
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
    // BLOCK waiter must be armed before write_fun_() runs.
    // 必须先挂起 BLOCK waiter，再调用 write_fun_()。
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
      ASSERT(expected == BusyState::BLOCK_DETACHED);
      busy_.store(BusyState::IDLE, std::memory_order_release);
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

void WritePort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  queue_data_->Reset();
  queue_info_->Reset();

  auto state = busy_.load(std::memory_order_acquire);
  if (state == BusyState::BLOCK_WAITING)
  {
    // Reset detaches the BLOCK waiter instead of reopening the port directly.
    // Reset 先分离 BLOCK waiter，不直接重开端口。
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

  block_result_ = ErrorCode::OK;
  busy_.store(BusyState::IDLE, std::memory_order_release);
}

WritePort::Stream::Stream(LibXR::WritePort* port, LibXR::WriteOperation op)
    : port_(port), op_(op)
{
  UNUSED(EnsureLocked());
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
  UNUSED(Append(data));

  return *this;
}

ErrorCode WritePort::Stream::EnsureLocked()
{
  if (locked_)
  {
    return ErrorCode::OK;
  }
  if (port_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  BusyState expected = BusyState::IDLE;
  if (!port_->busy_.compare_exchange_strong(expected, BusyState::LOCKED,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
  {
    return ErrorCode::BUSY;
  }

  if (port_->queue_info_->EmptySize() < 1)
  {
    port_->busy_.store(BusyState::IDLE, std::memory_order_release);
    return ErrorCode::FULL;
  }

  locked_ = true;
  cap_ = port_->queue_data_->EmptySize();
  return ErrorCode::OK;
}

ErrorCode WritePort::Stream::Append(ConstRawData data)
{
  if (data.size_ == 0)
  {
    return ErrorCode::OK;
  }

  auto ec = EnsureLocked();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }
  if (data.size_ > RemainingSize())
  {
    return ErrorCode::FULL;
  }

  auto ans =
      port_->queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
  ASSERT(ans == ErrorCode::OK);
  size_ += data.size_;
  return ErrorCode::OK;
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
