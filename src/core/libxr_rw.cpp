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
        // Reset detached this waiter before the timeout-side cancel won.
        // Reset 先分离了当前 waiter；超时侧负责把端口收回 IDLE。
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

    // The waiter may have timed out and released a reset-detached operation before
    // this late completion is reported.
    // waiter 可能已经超时并释放了 Reset 分离的操作，随后迟到完成才上报。
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

void WritePort::Reset()
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    auto state = busy_.load(std::memory_order_acquire);

    if (state == BusyState::LOCKED || state == BusyState::BLOCK_PUBLISHING ||
        state == BusyState::RESETTING)
    {
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
      queue_info_->Reset();
      block_result_ = ErrorCode::OK;
      busy_.store(BusyState::IDLE, std::memory_order_release);
      return;
    }

    if (state == BusyState::BLOCK_WAITING)
    {
      // Claim reset ownership before touching queues; late Finish() must not reopen
      // the port while Reset() is clearing queue state.
      // 先取得 reset 所有权再清队列；迟到 Finish() 不能在 Reset() 清队列期间重开端口。
      BusyState expected = BusyState::BLOCK_WAITING;
      if (!busy_.compare_exchange_strong(expected, BusyState::RESETTING,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        continue;
      }

      queue_data_->Reset();
      queue_info_->Reset();
      block_result_ = ErrorCode::OK;
      busy_.store(BusyState::BLOCK_DETACHED, std::memory_order_release);
      return;
    }

    if (state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_DETACHED)
    {
      return;
    }
  }
}

WritePort::Stream::Stream(LibXR::WritePort* port, LibXR::WriteOperation op)
    : port_(port), op_(op)
{
  UNUSED(Acquire());
}

// Stream batch helpers.
// Stream 批次辅助逻辑。
WritePort::Stream::~Stream()
{
  if (owns_port_ && buffered_size_ > 0)
  {
    UNUSED(SubmitBuffered());
  }

  if (owns_port_)
  {
    Release();
  }
}

ErrorCode WritePort::Stream::Acquire()
{
  if (owns_port_)
  {
    return ErrorCode::OK;
  }

  if (port_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  if (!port_->Writable())
  {
    return ErrorCode::NOT_SUPPORT;
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

  owns_port_ = true;
  batch_capacity_ = port_->queue_data_->EmptySize();
  return ErrorCode::OK;
}

ErrorCode WritePort::Stream::Write(ConstRawData data)
{
  if (data.size_ == 0)
  {
    return ErrorCode::OK;
  }

  if (data.addr_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  auto lock_result = Acquire();
  if (lock_result != ErrorCode::OK)
  {
    return lock_result;
  }

  auto ans = port_->queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                           data.size_);
  if (ans == ErrorCode::OK)
  {
    buffered_size_ += data.size_;
  }
  return ans;
}

ErrorCode WritePort::Stream::SubmitBuffered()
{
  ASSERT(owns_port_);
  ASSERT(buffered_size_ > 0);

  if (op_.type == WriteOperation::OperationType::BLOCK)
  {
    // Publish the wait state before the queued metadata can be consumed.
    // 元数据可能被消费前，先发布等待状态。
    op_.MarkAsRunning();
    port_->busy_.store(BusyState::BLOCK_PUBLISHING, std::memory_order_release);
  }

  auto ans = port_->queue_info_->Push(
      WriteInfoBlock{ConstRawData{nullptr, buffered_size_}, op_});
  ASSERT(ans == ErrorCode::OK);

  ans = port_->CommitWrite({nullptr, buffered_size_}, op_, true);
  buffered_size_ = 0;

  if (op_.type == WriteOperation::OperationType::BLOCK)
  {
    // WritePort now owns the BLOCK wait/finish state machine.
    // BLOCK 等待/完成状态机此后由 WritePort 接管。
    owns_port_ = false;
  }

  return ans;
}

void WritePort::Stream::Release()
{
  if (owns_port_)
  {
    owns_port_ = false;
    port_->busy_.store(BusyState::IDLE, std::memory_order_release);
  }
}

WritePort::Stream& WritePort::Stream::operator<<(const ConstRawData& data)
{
  if (Acquire() != ErrorCode::OK)
  {
    return *this;
  }

  if (EmptySize() < data.size_)
  {
    return *this;
  }

  UNUSED(Write(data));
  return *this;
}

ErrorCode WritePort::Stream::Commit()
{
  auto ans = ErrorCode::OK;

  if (owns_port_ && buffered_size_ > 0)
  {
    ans = SubmitBuffered();
    if (op_.type == WriteOperation::OperationType::BLOCK)
    {
      return ans;
    }
  }

  if (owns_port_)
  {
    Release();
  }

  return ans;
}

void WritePort::Stream::Discard()
{
  buffered_size_ = 0;
  Release();
}

// STDIO compiled-format bridge.
// STDIO 编译格式桥接层。
STDIO::CompiledSink::CompiledSink(WritePort::Stream& stream) : stream_(stream) {}

ErrorCode STDIO::CompiledSink::Write(std::string_view chunk)
{
  if (saturated_)
  {
    return ErrorCode::OK;
  }

  auto ec = stream_.Acquire();
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  size_t copy_size = chunk.size();
  size_t stream_writable = stream_.EmptySize();
  if (copy_size > stream_writable)
  {
    copy_size = stream_writable;
  }

  if (copy_size == 0)
  {
    saturated_ = true;
    return ErrorCode::OK;
  }

  ec = stream_.Write(chunk.substr(0, copy_size));
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  retained_size_ += copy_size;
  if (copy_size != chunk.size() || stream_.EmptySize() == 0)
  {
    saturated_ = true;
  }
  return ErrorCode::OK;
}

bool STDIO::BeginWriteSession()
{
  if (!STDIO::write_ || !STDIO::write_->Writable())
  {
    return false;
  }

  if (!write_mutex_)
  {
    write_mutex_ = new LibXR::Mutex();
  }

  return write_mutex_->Lock() == ErrorCode::OK;
}

int STDIO::WriteCompiledToStream(WritePort::Stream& stream, void* context,
                                 CompiledWriteFun write_fun)
{
  CompiledSink sink(stream);
  auto ec = write_fun(context, sink);
  return FinishWriteSession(stream, sink.RetainedSize(), ec);
}

int STDIO::WriteCompiledSession(void* context, CompiledWriteFun write_fun)
{
  ASSERT(write_mutex_ != nullptr);
  ASSERT(write_fun != nullptr);

  if (write_stream_ != nullptr)
  {
    return WriteCompiledToStream(*write_stream_, context, write_fun);
  }

  static WriteOperation op;  // NOLINT
  WritePort::Stream stream(write_, op);
  return WriteCompiledToStream(stream, context, write_fun);
}

int STDIO::FinishWriteSession(WritePort::Stream& stream, size_t retained_size,
                              ErrorCode format_result)
{
  ASSERT(write_mutex_ != nullptr);

  auto ec = format_result;
  if (ec == ErrorCode::OK)
  {
    ec = stream.Commit();
  }
  else
  {
    stream.Discard();
  }

  write_mutex_->Unlock();

  if (ec != ErrorCode::OK ||
      retained_size > static_cast<size_t>(std::numeric_limits<int>::max()))
  {
    return -1;
  }

  return static_cast<int>(retained_size);
}
