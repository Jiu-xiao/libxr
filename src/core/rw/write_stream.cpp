#include "write_port.hpp"

using namespace LibXR;

WritePort::Stream::Stream(LibXR::WritePort* port, LibXR::WriteOperation op)
    : port_(port), op_(op)
{
  UNUSED(Acquire());
}

// Stream batch helpers.
// Stream 批次辅助逻辑。
WritePort::Stream::~Stream() { UNUSED(Commit()); }

ErrorCode WritePort::Stream::Acquire()
{
  auto state = state_.load(std::memory_order_acquire);
  if (state == StreamState::SUBMITTING)
  {
    return ErrorCode::BUSY;
  }

  if (state == StreamState::OWNED)
  {
    return ErrorCode::OK;
  }

  StreamState expected_state = StreamState::RELEASED;
  if (!state_.compare_exchange_strong(expected_state, StreamState::SUBMITTING,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
    return expected_state == StreamState::OWNED ? ErrorCode::OK : ErrorCode::BUSY;
  }

  if (port_ == nullptr)
  {
    state_.store(StreamState::RELEASED, std::memory_order_release);
    return ErrorCode::PTR_NULL;
  }

  DEV_ASSERT(port_->queue_info_ != nullptr);
  DEV_ASSERT(port_->queue_data_ != nullptr);

  if (!port_->Writable())
  {
    state_.store(StreamState::RELEASED, std::memory_order_release);
    return ErrorCode::NOT_SUPPORT;
  }

  if (!port_->TryAcquireOwner())
  {
    state_.store(StreamState::RELEASED, std::memory_order_release);
    return ErrorCode::BUSY;
  }

  if (port_->queue_info_->EmptySize() < 1)
  {
    port_->ReleaseOwner(false);
    state_.store(StreamState::RELEASED, std::memory_order_release);
    return ErrorCode::FULL;
  }

  state_.store(StreamState::OWNED, std::memory_order_release);
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
  StreamState expected = StreamState::OWNED;
  if (!state_.compare_exchange_strong(expected, StreamState::SUBMITTING,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
    return ErrorCode::BUSY;
  }
  ASSERT(buffered_size_ > 0);

  const size_t submitted_size = buffered_size_;
  const auto ans = port_->CommitWrite({nullptr, submitted_size}, op_, true);
  buffered_size_ = 0;

  // Reopen Stream admission only after both local metadata and Port ownership are final.
  state_.store(StreamState::RELEASED, std::memory_order_release);
  return ans;
}

size_t WritePort::Stream::EmptySize() const
{
  return state_.load(std::memory_order_acquire) == StreamState::OWNED
             ? port_->queue_data_->EmptySize()
             : 0U;
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
  const auto state = state_.load(std::memory_order_acquire);
  if (state == StreamState::SUBMITTING)
  {
    return ErrorCode::BUSY;
  }

  if (state == StreamState::RELEASED)
  {
    return ErrorCode::OK;
  }

  if (buffered_size_ > 0)
  {
    return SubmitBuffered();
  }

  StreamState expected = StreamState::OWNED;
  if (!state_.compare_exchange_strong(expected, StreamState::SUBMITTING,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
  {
    return ErrorCode::BUSY;
  }
  port_->ReleaseOwner(false);
  state_.store(StreamState::RELEASED, std::memory_order_release);
  return ErrorCode::OK;
}
