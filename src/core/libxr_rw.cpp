#include "libxr_rw.hpp"

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

void ReadPort::Finish(bool in_isr, ErrorCode ans, ReadInfoBlock& info, uint32_t size)
{
  read_size_ = size;
  busy_.store(BusyState::Idle, std::memory_order_release);
  info.op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
}

void ReadPort::MarkAsRunning(ReadInfoBlock& info) { info.op.MarkAsRunning(); }

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op)
{
  if (Readable())
  {
    mutex_.Lock();

    BusyState is_busy = busy_.load(std::memory_order_relaxed);

    if (is_busy == BusyState::Pending)
    {
      mutex_.Unlock();
      return ErrorCode::BUSY;
    }

    while (true)
    {
      busy_.store(BusyState::Idle, std::memory_order_release);

      if (queue_data_ != nullptr)
      {
        auto readable_size = queue_data_->Size();

        if (readable_size >= data.size_ && readable_size != 0)
        {
          auto ans =
              queue_data_->PopBatch(reinterpret_cast<uint8_t*>(data.addr_), data.size_);
          UNUSED(ans);
          read_size_ = data.size_;
          ASSERT(ans == ErrorCode::OK);
          if (op.type != ReadOperation::OperationType::BLOCK)
          {
            op.UpdateStatus(false, ErrorCode::OK);
          }
          mutex_.Unlock();
          return ErrorCode::OK;
        }
      }

      info_ = ReadInfoBlock{data, op};

      op.MarkAsRunning();

      auto ans = read_fun_(*this);

      if (ans != ErrorCode::OK)
      {
        BusyState expected = BusyState::Idle;
        if (busy_.compare_exchange_strong(expected, BusyState::Pending,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire))
        {
          break;
        }
        else
        {
          expected = BusyState::Pending;
          continue;
        }
      }
      else
      {
        read_size_ = data.size_;
        if (op.type != ReadOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(false, ErrorCode::OK);
        }
        mutex_.Unlock();
        return ErrorCode::OK;
      }
    }

    mutex_.Unlock();

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

  if (in_isr)
  {
    auto is_busy = busy_.load(std::memory_order_relaxed);

    if (is_busy == BusyState::Pending)
    {
      if (queue_data_->Size() >= info_.data.size_)
      {
        if (info_.data.size_ > 0)
        {
          auto ans = queue_data_->PopBatch(reinterpret_cast<uint8_t*>(info_.data.addr_),
                                           info_.data.size_);
          UNUSED(ans);
          ASSERT(ans == ErrorCode::OK);
        }
        Finish(in_isr, ErrorCode::OK, info_, info_.data.size_);
      }
    }
    else if (is_busy == BusyState::Idle)
    {
      busy_.store(BusyState::Event, std::memory_order_release);
    }
  }
  else
  {
    LibXR::Mutex::LockGuard lock_guard(mutex_);
    if (busy_.load(std::memory_order_relaxed) == BusyState::Pending)
    {
      if (queue_data_->Size() >= info_.data.size_)
      {
        if (info_.data.size_ > 0)
        {
          auto ans = queue_data_->PopBatch(reinterpret_cast<uint8_t*>(info_.data.addr_),
                                           info_.data.size_);
          UNUSED(ans);
          ASSERT(ans == ErrorCode::OK);
        }
        Finish(in_isr, ErrorCode::OK, info_, info_.data.size_);
      }
    }
  }
}

void ReadPort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  Mutex::LockGuard lock_guard(mutex_);
  queue_data_->Reset();
  read_size_ = 0;
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

void WritePort::Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info, uint32_t size)
{
  write_size_ = size;
  info.op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
}

void WritePort::MarkAsRunning(WriteOperation& op) { op.MarkAsRunning(); }

ErrorCode WritePort::operator()(ConstRawData data, WriteOperation& op)
{
  if (Writable())
  {
    if (data.size_ == 0)
    {
      write_size_ = 0;
      if (op.type != WriteOperation::OperationType::BLOCK)
      {
        op.UpdateStatus(false, ErrorCode::OK);
      }
      return ErrorCode::OK;
    }

    mutex_.Lock();

    if (queue_info_->EmptySize() < 1)
    {
      mutex_.Unlock();
      return ErrorCode::FULL;
    }

    if (queue_data_)
    {
      if (queue_data_->EmptySize() < data.size_)
      {
        mutex_.Unlock();
        return ErrorCode::FULL;
      }

      auto ans = queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                        data.size_);
      UNUSED(ans);
      ASSERT(ans == ErrorCode::OK);

      WriteInfoBlock info{data, op};
      ans = queue_info_->Push(info);

      ASSERT(ans == ErrorCode::OK);

      op.MarkAsRunning();

      ans = write_fun_(*this);

      mutex_.Unlock();

      if (ans == ErrorCode::OK)
      {
        write_size_ = data.size_;
        if (op.type != WriteOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(false, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }

      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }

      return ErrorCode::OK;
    }
    else
    {
      WriteInfoBlock info{data, op};
      auto ans = queue_info_->Push(info);

      ASSERT(ans == ErrorCode::OK);

      op.MarkAsRunning();

      ans = write_fun_(*this);

      mutex_.Unlock();

      if (ans == ErrorCode::OK)
      {
        write_size_ = data.size_;
        if (op.type != WriteOperation::OperationType::BLOCK)
        {
          op.UpdateStatus(false, ErrorCode::OK);
        }
        return ErrorCode::OK;
      }

      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }
      else
      {
        return ErrorCode::OK;
      }
    }
  }
  else
  {
    return ErrorCode::NOT_SUPPORT;
  }
}

void WritePort::Reset()
{
  ASSERT(queue_data_ != nullptr);
  Mutex::LockGuard lock_guard(mutex_);
  queue_info_->Reset();
  queue_data_->Reset();
  write_size_ = 0;
}

int STDIO::Printf(const char* fmt, ...)
{
#if LIBXR_PRINTF_BUFFER_SIZE > 0
  if (!STDIO::write_ || !STDIO::write_->Writable())
  {
    return -1;
  }

  static LibXR::Mutex mutex;  // NOLINT

  LibXR::Mutex::LockGuard lock_guard(mutex);

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
  return static_cast<int>(STDIO::write_->operator()(data, op));
#else
  UNUSED(fmt);
  return 0;
#endif
}
