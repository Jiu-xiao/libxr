#include "write_port.hpp"

using namespace LibXR;

WritePort::Stream::Stream(WritePort* port, WriteOperation op) : port_(port), op_(op)
{
  UNUSED(Acquire());
}

WritePort::Stream::~Stream()
{
  if (!owns_port_)
  {
    return;
  }

  if (buffered_size_ != 0U)
  {
    UNUSED(SubmitBuffered());
    return;
  }

  Release();
  CompleteEmpty();
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
  if (!port_->TryClaimProducer())
  {
    return ErrorCode::BUSY;
  }

  if (!port_->IsAdmissionMode() && port_->queue_requests_->EmptySize() < 1U)
  {
    port_->ReleaseProducer(WritePort::Phase::IDLE, false);
    return ErrorCode::FULL;
  }

  owns_port_ = true;
  return ErrorCode::OK;
}

ErrorCode WritePort::Stream::Write(ConstRawData data)
{
  if (port_ == nullptr || !port_->Writable())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (data.size_ == 0U)
  {
    return ErrorCode::OK;
  }

  const ErrorCode acquire_result = Acquire();
  if (acquire_result != ErrorCode::OK)
  {
    return acquire_result;
  }

  REQUIRE(data.addr_ != nullptr);

  if (port_->queue_data_ == nullptr || port_->queue_data_->EmptySize() < data.size_)
  {
    return ErrorCode::FULL;
  }

  const ErrorCode result = port_->queue_data_->PushBatch(
      reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
  REQUIRE(result == ErrorCode::OK);
  buffered_size_ += data.size_;
  return ErrorCode::OK;
}

ErrorCode WritePort::Stream::SubmitBuffered()
{
  REQUIRE(owns_port_);
  REQUIRE(buffered_size_ != 0U);

  const size_t size = buffered_size_;
  buffered_size_ = 0U;
  owns_port_ = false;
  return port_->IsAdmissionMode() ? port_->CommitAdmission(size, op_, false)
                                  : port_->CommitQueued(size, op_, false);
}

void WritePort::Stream::Release()
{
  if (!owns_port_)
  {
    return;
  }
  REQUIRE(buffered_size_ == 0U);
  owns_port_ = false;
  port_->ReleaseProducer(WritePort::Phase::IDLE, false);
}

void WritePort::Stream::CompleteEmpty()
{
  if (op_.type != WriteOperation::OperationType::BLOCK)
  {
    op_.UpdateStatus(false, ErrorCode::OK);
  }
}

WritePort::Stream& WritePort::Stream::operator<<(const ConstRawData& data)
{
  if (Acquire() == ErrorCode::OK && EmptySize() >= data.size_)
  {
    UNUSED(Write(data));
  }
  return *this;
}

ErrorCode WritePort::Stream::Commit()
{
  if (!owns_port_)
  {
    return ErrorCode::OK;
  }
  if (buffered_size_ != 0U)
  {
    return SubmitBuffered();
  }

  Release();
  CompleteEmpty();
  return ErrorCode::OK;
}
