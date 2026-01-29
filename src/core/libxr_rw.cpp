#include "libxr_rw.hpp"

#include "mutex.hpp"

using namespace LibXR;

template class LibXR::LockFreeQueue<WriteInfoBlock>;
template class LibXR::LockFreeQueue<uint8_t>;

ReadPort::ReadPort(size_t buffer_size)
    : queue_data_(buffer_size > 0 ? new(std::align_val_t(LIBXR_CACHE_LINE_SIZE))
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
  busy_.store(BusyState::IDLE, std::memory_order_release);
  info.op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
}

void ReadPort::MarkAsRunning(ReadInfoBlock& info) { info.op.MarkAsRunning(); }

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op, bool in_isr)
{
  if (Readable())
  {
    BusyState is_busy = busy_.load(std::memory_order_relaxed);

    if (is_busy == BusyState::PENDING)
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
        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(in_isr, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }

      info_ = ReadInfoBlock{data, op};

      op.MarkAsRunning();

      auto ans = read_fun_(*this, in_isr);

      if (ans != ErrorCode::OK)
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
        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(in_isr, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }
    }

    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
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
      if (info_.data.size_ > 0)
      {
        auto ans = queue_data_->PopBatch(reinterpret_cast<uint8_t*>(info_.data.addr_),
                                         info_.data.size_);
        UNUSED(ans);
        ASSERT(ans == ErrorCode::OK);
      }
      Finish(in_isr, ErrorCode::OK, info_);
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
}

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_info_(new(std::align_val_t(LIBXR_CACHE_LINE_SIZE))
                      LockFreeQueue<WriteInfoBlock>(queue_size)),
      queue_data_(buffer_size > 0 ? new(std::align_val_t(LIBXR_CACHE_LINE_SIZE))
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
  info.op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
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

    LockState expected = LockState::UNLOCKED;
    if (!lock_.compare_exchange_strong(expected, LockState::LOCKED))
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
    lock_.store(LockState::UNLOCKED, std::memory_order_release);
    return ErrorCode::FULL;
  }

  ErrorCode ans = ErrorCode::OK;
  if (!meta_pushed)
  {
    if (queue_data_->EmptySize() < data.size_)
    {
      lock_.store(LockState::UNLOCKED, std::memory_order_release);
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

  ans = write_fun_(*this, in_isr);

  if (!meta_pushed)
  {
    lock_.store(LockState::UNLOCKED, std::memory_order_release);
  }

  if (ans == ErrorCode::OK)
  {
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ErrorCode::OK);
    }
    return ErrorCode::OK;
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
  }

  return ErrorCode::OK;
}

void WritePort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  queue_data_->Reset();
  queue_info_->Reset();
}

WritePort::Stream::Stream(LibXR::WritePort* port, LibXR::WriteOperation op)
    : port_(port), op_(op)
{
  LockState expected = LockState::UNLOCKED;
  if (port_->lock_.compare_exchange_strong(expected, LockState::LOCKED))
  {
    if (port_->queue_info_->EmptySize() < 1)
    {
      locked_ = false;
      port_->lock_.store(LockState::UNLOCKED, std::memory_order_release);
      return;
    }
    locked_ = true;
    cap_ = port_->queue_data_->EmptySize();
  }
}

WritePort::Stream::~Stream()
{
  if (locked_ && size_ > 0)
  {
    port_->queue_info_->Push(WriteInfoBlock{ConstRawData{nullptr, size_}, op_});
    port_->CommitWrite({nullptr, size_}, op_, true);
  }

  if (locked_)
  {
    port_->lock_.store(LockState::UNLOCKED, std::memory_order_release);
  }
}

WritePort::Stream& WritePort::Stream::operator<<(const ConstRawData& data)
{
  if (!locked_)
  {
    LockState expected = LockState::UNLOCKED;
    if (port_->lock_.compare_exchange_strong(expected, LockState::LOCKED))
    {
      if (port_->queue_info_->EmptySize() < 1)
      {
        locked_ = false;
        port_->lock_.store(LockState::UNLOCKED, std::memory_order_release);
        return *this;
      }
      else
      {
        locked_ = true;
        cap_ = port_->queue_data_->EmptySize();
      }
    }
    else
    {
      return *this;
    }
  }
  if (size_ + data.size_ <= cap_)
  {
    port_->queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                  data.size_);
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
    ASSERT(ans == ErrorCode::OK);
    size_ = 0;
  }

  if (port_->queue_info_->EmptySize() < 1)
  {
    locked_ = false;
    port_->lock_.store(LockState::UNLOCKED, std::memory_order_release);
  }
  else
  {
    cap_ = port_->queue_data_->EmptySize();
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
