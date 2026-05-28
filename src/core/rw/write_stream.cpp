#include "write_port.hpp"

using namespace LibXR;

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

  DEV_ASSERT(port_->queue_info_ != nullptr);
  DEV_ASSERT(port_->queue_data_ != nullptr);

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
