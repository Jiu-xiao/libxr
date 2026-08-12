#include "write_port.hpp"

#include <new>

using namespace LibXR;

template class LibXR::SPSCQueue<WriteInfoBlock>;
template class LibXR::SPSCQueue<uint8_t>;

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_info_(new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                      SPSCQueue<WriteInfoBlock>(queue_size)),
      queue_data_(buffer_size > 0 ? new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                                        SPSCQueue<uint8_t>(buffer_size)
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

bool WritePort::TryAcquireOwner()
{
  StateWord expected = busy_.load(std::memory_order_acquire);
  if (IsSubmissionCompletion(expected) || SubmissionPhase(expected) != StatePhase::IDLE)
  {
    return false;
  }

  return busy_.compare_exchange_strong(
      expected, PhaseState(SubmissionGeneration(expected), StatePhase::OWNER),
      std::memory_order_acq_rel, std::memory_order_acquire);
}

void WritePort::ReleaseOwner()
{
  StateWord expected = busy_.load(std::memory_order_acquire);
  REQUIRE(!IsSubmissionCompletion(expected));
  REQUIRE(SubmissionPhase(expected) == StatePhase::OWNER);
  const bool released = busy_.compare_exchange_strong(
      expected, PhaseState(SubmissionGeneration(expected), StatePhase::IDLE),
      std::memory_order_acq_rel, std::memory_order_acquire);
  REQUIRE(released);
}

uint32_t WritePort::BeginSubmission(WriteOperation::OperationType type)
{
  StateWord expected = busy_.load(std::memory_order_acquire);
  REQUIRE(!IsSubmissionCompletion(expected));
  REQUIRE(SubmissionPhase(expected) == StatePhase::OWNER);

  const uint32_t submission_id = NextSubmissionGeneration(SubmissionGeneration(expected));
  const StatePhase phase = type == WriteOperation::OperationType::BLOCK
                               ? StatePhase::BLOCK_WAITING
                               : StatePhase::SUBMITTING;
  const bool published =
      busy_.compare_exchange_strong(expected, PhaseState(submission_id, phase),
                                    std::memory_order_acq_rel, std::memory_order_acquire);
  REQUIRE(published);
  return submission_id;
}

ErrorCode WritePort::ResolveSubmission(uint32_t submission_id, ErrorCode backend_result)
{
  StateWord expected = PhaseState(submission_id, StatePhase::SUBMITTING);
  if (busy_.compare_exchange_strong(expected, PhaseState(submission_id, StatePhase::IDLE),
                                    std::memory_order_acq_rel, std::memory_order_acquire))
  {
    return backend_result;
  }

  REQUIRE(SubmissionGeneration(expected) == submission_id);
  REQUIRE(IsSubmissionCompletion(expected));
  const ErrorCode completion_result = SubmissionCompletionResult(expected);
  REQUIRE(backend_result == ErrorCode::PENDING || backend_result == completion_result);
  const bool consumed =
      busy_.compare_exchange_strong(expected, PhaseState(submission_id, StatePhase::IDLE),
                                    std::memory_order_acq_rel, std::memory_order_acquire);
  REQUIRE(consumed);
  return completion_result;
}

ErrorCode WritePort::ConsumeBlockCompletion(uint32_t submission_id)
{
  StateWord expected = busy_.load(std::memory_order_acquire);
  REQUIRE(SubmissionGeneration(expected) == submission_id);
  REQUIRE(IsSubmissionCompletion(expected));
  const ErrorCode result = SubmissionCompletionResult(expected);
  const bool consumed =
      busy_.compare_exchange_strong(expected, PhaseState(submission_id, StatePhase::IDLE),
                                    std::memory_order_acq_rel, std::memory_order_acquire);
  REQUIRE(consumed);
  return result;
}

bool WritePort::TryCaptureSubmissionCompletion(bool in_isr, uint32_t submission_id,
                                               ErrorCode result)
{
  StateWord expected = PhaseState(submission_id, StatePhase::SUBMITTING);
  if (busy_.compare_exchange_strong(expected,
                                    SubmissionCompletionState(submission_id, result),
                                    std::memory_order_acq_rel, std::memory_order_acquire))
  {
    return true;
  }

  if (SubmissionGeneration(expected) == submission_id && IsSubmissionCompletion(expected))
  {
    REQUIRE_FROM_CALLBACK(false, in_isr);
    return true;
  }
  return false;
}

void WritePort::Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info)
{
  REQUIRE_FROM_CALLBACK(ans != ErrorCode::PENDING, in_isr);
  const uint32_t submission_id = info.submission_id;
  REQUIRE_FROM_CALLBACK(submission_id != UINT32_MAX, in_isr);
  info.submission_id = UINT32_MAX;

  if (info.op.type == WriteOperation::OperationType::BLOCK)
  {
    StateWord expected = PhaseState(submission_id, StatePhase::BLOCK_WAITING);
    if (busy_.compare_exchange_strong(
            expected, SubmissionCompletionState(submission_id, ans),
            std::memory_order_acq_rel, std::memory_order_acquire))
    {
      info.op.data.sem_info.sem->PostFromCallback(in_isr);
      return;
    }

    const StateWord detached = PhaseState(submission_id, StatePhase::BLOCK_DETACHED);
    if (expected == detached)
    {
      const bool released = busy_.compare_exchange_strong(
          expected, PhaseState(submission_id, StatePhase::IDLE),
          std::memory_order_acq_rel, std::memory_order_acquire);
      REQUIRE_FROM_CALLBACK(released, in_isr);
      return;
    }

    REQUIRE_FROM_CALLBACK(false, in_isr);
    return;
  }

  if (TryCaptureSubmissionCompletion(in_isr, submission_id, ans))
  {
    return;
  }

  info.op.UpdateStatus(in_isr, ans);
}

void WritePort::MarkAsRunning(WriteOperation& op) { op.MarkAsRunning(); }

ErrorCode WritePort::operator()(ConstRawData data, WriteOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(op.type != WriteOperation::OperationType::BLOCK || !in_isr,
                        in_isr);

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

    if (!TryAcquireOwner())
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
                                 bool in_isr, uint32_t submission_id)
{
  if (!meta_pushed && queue_info_->EmptySize() < 1)
  {
    ReleaseOwner();
    return ErrorCode::FULL;
  }

  ErrorCode ans = ErrorCode::OK;
  if (!meta_pushed)
  {
    if (queue_data_->EmptySize() < data.size_)
    {
      ReleaseOwner();
      return ErrorCode::FULL;
    }

    ans =
        queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
    UNUSED(ans);
    ASSERT(ans == ErrorCode::OK);

    // Operation state is part of the published queue record. Arm it before metadata
    // becomes visible to a concurrent backend/configuration owner.
    // operation 状态属于已发布队列记录的一部分；在 metadata 对并发 backend/config owner
    // 可见前先挂起。
    op.MarkAsRunning();
    submission_id = BeginSubmission(op.type);
    WriteInfoBlock info{data, op, submission_id};
    ans = queue_info_->Push(info);

    ASSERT(ans == ErrorCode::OK);
  }
  else
  {
    REQUIRE(submission_id != UINT32_MAX);
  }

  ans = write_fun_(*this, in_isr);

  if (op.type != WriteOperation::OperationType::BLOCK)
  {
    ans = ResolveSubmission(submission_id, ans);
    if (ans != ErrorCode::PENDING)
    {
      op.UpdateStatus(in_isr, ans);
      return (static_cast<int8_t>(ans) < 0) ? ans : ErrorCode::OK;
    }
    return ErrorCode::OK;
  }

  if (ans != ErrorCode::PENDING)
  {
    StateWord expected = PhaseState(submission_id, StatePhase::BLOCK_WAITING);
    if (busy_.compare_exchange_strong(
            expected, PhaseState(submission_id, StatePhase::IDLE),
            std::memory_order_acq_rel, std::memory_order_acquire))
    {
      return ans;
    }

    REQUIRE(SubmissionGeneration(expected) == submission_id);
    REQUIRE(IsSubmissionCompletion(expected));
    REQUIRE(SubmissionCompletionResult(expected) == ans);
    const auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
    REQUIRE(finish_wait_ans == ErrorCode::OK);
    return ConsumeBlockCompletion(submission_id);
  }

  ASSERT(!in_isr);
  const auto wait_ans = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
  if (wait_ans == ErrorCode::OK)
  {
    return ConsumeBlockCompletion(submission_id);
  }

  REQUIRE(wait_ans == ErrorCode::TIMEOUT);
  StateWord expected = PhaseState(submission_id, StatePhase::BLOCK_WAITING);
  if (busy_.compare_exchange_strong(expected,
                                    PhaseState(submission_id, StatePhase::BLOCK_DETACHED),
                                    std::memory_order_acq_rel, std::memory_order_acquire))
  {
    return ErrorCode::TIMEOUT;
  }

  REQUIRE(SubmissionGeneration(expected) == submission_id);
  REQUIRE(IsSubmissionCompletion(expected));

  // Completion owns the outcome once it publishes the result, even if its semaphore
  // post loses the race with the original timeout.
  const auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
  REQUIRE(finish_wait_ans == ErrorCode::OK);
  return ConsumeBlockCompletion(submission_id);
}
