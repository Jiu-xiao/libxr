#include "write_port.hpp"

#include <new>

using namespace LibXR;

template class LibXR::SPSCQueue<WriteInfoBlock>;
template class LibXR::SPSCQueue<uint8_t>;

namespace
{
ErrorCode NormalizeSynchronousResult(ErrorCode result)
{
  return static_cast<int8_t>(result) < 0 ? result : ErrorCode::OK;
}
}  // namespace

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
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::FREE && Active(observed) == ActiveState::NONE &&
         !HasHandoff(observed))
  {
    uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::OWNER));
    desired = WithResult(desired, ErrorCode::OK);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return true;
    }
  }
  return false;
}

bool WritePort::TryReserveDeferredOwner()
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    const ActiveState active = Active(observed);
    if (CurrentPhase(observed) != Phase::FREE ||
        (active != ActiveState::NONE && active != ActiveState::DETACHED) ||
        HasHandoff(observed))
    {
      return false;
    }

    const uint32_t desired = WithPhase(observed & ~PUBLISH_TIMEOUT, Phase::OWNER);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return true;
    }
  }
}

void WritePort::ReleaseOwner(bool in_isr)
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::OWNER && Active(observed) == ActiveState::NONE)
  {
    uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
    desired = WithResult(desired, ErrorCode::OK);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return;
    }
  }
  ASSERT_FROM_CALLBACK(false, in_isr);
}

void WritePort::ReleaseBlockClaim()
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::FREE &&
         Active(observed) == ActiveState::CLAIMED)
  {
    uint32_t desired = WithoutPublicationFlags(WithActive(observed, ActiveState::NONE));
    desired = WithResult(desired, ErrorCode::OK);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return;
    }
  }
  ASSERT(false);
}

void WritePort::Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info)
{
  if (info.op.type != WriteOperation::OperationType::BLOCK)
  {
    info.op.UpdateStatus(in_isr, ans);
    ProcessPendingWrites(in_isr);
    return;
  }

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    const ActiveState active = Active(observed);
    if (active == ActiveState::WAITING)
    {
      const uint32_t desired =
          WithResult(WithActive(observed, ActiveState::CLAIMED), ans);
      if (!state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        continue;
      }

      // A synchronous Finish only records the result. The publisher releases its phase
      // before it performs the unique wakeup.
      if (CurrentPhase(desired) == Phase::FREE)
      {
        info.op.data.sem_info.sem->PostFromCallback(in_isr);
        ProcessPendingWrites(in_isr);
      }
      return;
    }

    if (active == ActiveState::DETACHED)
    {
      const uint32_t desired = WithActive(observed, ActiveState::NONE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        ProcessPendingWrites(in_isr);
        return;
      }
      continue;
    }

    ASSERT_FROM_CALLBACK(false, in_isr);
    return;
  }
}

void WritePort::MarkAsRunning(WriteOperation& op) { op.MarkAsRunning(); }

ErrorCode WritePort::operator()(ConstRawData data, WriteOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(op.type != WriteOperation::OperationType::BLOCK || !in_isr,
                        in_isr);

  if (!Writable())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (data.size_ == 0U)
  {
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ErrorCode::OK);
    }
    return ErrorCode::OK;
  }

  if (data.addr_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  ASSERT(queue_data_ != nullptr);
  if (data.size_ > queue_data_->MaxSize())
  {
    return ErrorCode::SIZE_ERR;
  }

  if (TryAcquireOwner())
  {
    return CommitWrite(data, op, false, in_isr);
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    return DeferBlock(data, op, false);
  }

  return ErrorCode::BUSY;
}

ErrorCode WritePort::DeferBlock(ConstRawData data, WriteOperation& op, bool owns_port)
{
  if (!owns_port && !TryReserveDeferredOwner())
  {
    return ErrorCode::BUSY;
  }

  deferred_info_ = WriteInfoBlock{data, op};

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::OWNER)
  {
    const ActiveState active = Active(observed);
    ASSERT(active == ActiveState::NONE || active == ActiveState::DETACHED);
    const uint32_t waiting = WithPhase(observed, Phase::WAITING);
    if (state_.compare_exchange_weak(observed, waiting, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      break;
    }
  }
  ASSERT(CurrentPhase(observed) == Phase::OWNER);

  // Close progress that occurred while deferred_info_ was not yet readable.
  ProcessPendingWrites(false);
  return WaitForBlock(op, true);
}

ErrorCode WritePort::CommitWrite(ConstRawData data, WriteOperation& op, bool data_pushed,
                                 bool in_isr)
{
  ASSERT(queue_info_ != nullptr);
  ASSERT(queue_data_ != nullptr);

  if (!data_pushed &&
      (queue_info_->EmptySize() < 1U || queue_data_->EmptySize() < data.size_))
  {
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return DeferBlock(data, op, true);
    }

    ReleaseOwner(in_isr);
    return ErrorCode::FULL;
  }

  ASSERT(queue_info_->EmptySize() >= 1U);
  return PublishOwned(data, op, data_pushed, in_isr, false);
}

ErrorCode WritePort::PublishOwned(ConstRawData data, WriteOperation& op, bool data_pushed,
                                  bool in_isr, bool deferred)
{
  const bool block = op.type == WriteOperation::OperationType::BLOCK;
  if (block)
  {
    const Phase expected_phase = deferred ? Phase::PUBLISHING : Phase::OWNER;
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (CurrentPhase(observed) == expected_phase &&
           Active(observed) == ActiveState::NONE)
    {
      const uint32_t desired = WithActive(observed, ActiveState::WAITING);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        break;
      }
    }
    ASSERT(CurrentPhase(observed) == expected_phase &&
           Active(observed) == ActiveState::NONE);
  }

  if (!data_pushed)
  {
    const ErrorCode data_result =
        queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
    UNUSED(data_result);
    ASSERT(data_result == ErrorCode::OK);
  }

  op.MarkAsRunning();
  const ConstRawData published_data =
      data_pushed ? ConstRawData{nullptr, data.size_} : data;
  const ErrorCode info_result = queue_info_->Push(WriteInfoBlock{published_data, op});
  UNUSED(info_result);
  ASSERT(info_result == ErrorCode::OK);

  const ErrorCode ans = write_fun_(*this, in_isr);

  if (!block)
  {
    // Release port admission before a synchronous terminal callback. A Stream keeps its
    // own SUBMITTING state until its local batch is finalized.
    ReleaseOwner(in_isr);
    if (ans != ErrorCode::PENDING)
    {
      op.UpdateStatus(in_isr, ans);
      return NormalizeSynchronousResult(ans);
    }
    return ErrorCode::OK;
  }

  if (deferred)
  {
    FinishDeferredPublication(op, ans, in_isr);
    return NormalizeSynchronousResult(ans);
  }

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    ASSERT_FROM_CALLBACK(CurrentPhase(observed) == Phase::OWNER, in_isr);
    const ActiveState active = Active(observed);

    if (active == ActiveState::CLAIMED)
    {
      const uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        op.data.sem_info.sem->PostFromCallback(in_isr);
        return WaitForBlock(op, false);
      }
      continue;
    }

    ASSERT_FROM_CALLBACK(active == ActiveState::WAITING, in_isr);
    if (ans == ErrorCode::PENDING)
    {
      const uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return WaitForBlock(op, false);
      }
      continue;
    }

    uint32_t desired = WithoutPublicationFlags(
        WithActive(WithPhase(observed, Phase::FREE), ActiveState::NONE));
    desired = WithResult(desired, ErrorCode::OK);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return NormalizeSynchronousResult(ans);
    }
  }
}

void WritePort::FinishDeferredPublication(WriteOperation& op, ErrorCode ans, bool in_isr)
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    ASSERT_FROM_CALLBACK(CurrentPhase(observed) == Phase::PUBLISHING, in_isr);
    const ActiveState active = Active(observed);

    if (active == ActiveState::CLAIMED)
    {
      const uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
      continue;
    }

    ASSERT_FROM_CALLBACK(active == ActiveState::WAITING, in_isr);
    if (ans != ErrorCode::PENDING)
    {
      uint32_t desired = WithoutPublicationFlags(
          WithActive(WithPhase(observed, Phase::FREE), ActiveState::CLAIMED));
      desired = WithResult(desired, NormalizeSynchronousResult(ans));
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
      continue;
    }

    const bool timed_out = HasPublishTimeout(observed);
    uint32_t desired = WithoutPublicationFlags(
        WithActive(WithPhase(observed, Phase::FREE),
                   timed_out ? ActiveState::DETACHED : ActiveState::WAITING));
    if (timed_out)
    {
      desired = WithResult(desired | HANDOFF, ErrorCode::TIMEOUT);
    }
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      if (timed_out)
      {
        // This wakeup ends the caller-buffer handoff only. The late backend Finish sees
        // DETACHED and must not post this semaphore again.
        op.data.sem_info.sem->PostFromCallback(in_isr);
      }
      return;
    }
  }
}

ErrorCode WritePort::WaitForBlock(WriteOperation& op, bool deferred_wait)
{
  ErrorCode wait_result = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);

  while (true)
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    const Phase phase = CurrentPhase(observed);
    const ActiveState active = Active(observed);

    if (wait_result == ErrorCode::OK)
    {
      if (phase == Phase::FREE && active == ActiveState::CLAIMED)
      {
        const ErrorCode result = Result(observed);
        ReleaseBlockClaim();
        return result;
      }

      if (deferred_wait && HasHandoff(observed))
      {
        const ErrorCode result = Result(observed);
        uint32_t desired = WithResult(observed & ~HANDOFF, ErrorCode::OK);
        if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                         std::memory_order_acquire))
        {
          return result;
        }
        continue;
      }

      ASSERT(false);
      return ErrorCode::STATE_ERR;
    }

    if (deferred_wait && HasHandoff(observed))
    {
      // The terminal publisher owns one semaphore post. Consume it before returning so
      // the caller may safely reuse its semaphore for a later operation.
      wait_result = op.data.sem_info.sem->Wait(UINT32_MAX);
      REQUIRE(wait_result == ErrorCode::OK);
      continue;
    }

    if (deferred_wait && phase == Phase::WAITING)
    {
      uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
      desired = WithResult(desired, ErrorCode::TIMEOUT);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }
      continue;
    }

    if (deferred_wait && phase == Phase::PUBLISHING)
    {
      const uint32_t desired = observed | PUBLISH_TIMEOUT;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        wait_result = op.data.sem_info.sem->Wait(UINT32_MAX);
        REQUIRE(wait_result == ErrorCode::OK);
      }
      continue;
    }

    if (phase == Phase::FREE && active == ActiveState::WAITING)
    {
      const uint32_t desired = WithActive(observed, ActiveState::DETACHED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }
      continue;
    }

    if (phase == Phase::OWNER && active == ActiveState::WAITING)
    {
      // A quiesced teardown owns the queues. It will either publish the final result or
      // observe DETACHED before deciding whether this waiter still owns a wakeup.
      const uint32_t desired = WithActive(observed, ActiveState::DETACHED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return ErrorCode::TIMEOUT;
      }
      continue;
    }

    if (active == ActiveState::CLAIMED || (deferred_wait && phase == Phase::OWNER))
    {
      wait_result = op.data.sem_info.sem->Wait(UINT32_MAX);
      REQUIRE(wait_result == ErrorCode::OK);
      continue;
    }

    ASSERT(false);
    return ErrorCode::STATE_ERR;
  }
}

void WritePort::ProcessPendingWrites(bool in_isr)
{
  ASSERT(queue_info_ != nullptr);
  ASSERT(queue_data_ != nullptr);

  // Every progress source contributes a fresh RMW, even when KICK is already set. This
  // carries the latest queue-head release into the promoter's acquire sequence.
  state_.fetch_or(KICK, std::memory_order_acq_rel);

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    if (CurrentPhase(observed) != Phase::WAITING ||
        Active(observed) != ActiveState::NONE || HasHandoff(observed))
    {
      return;
    }

    const uint32_t publishing = WithPhase(observed, Phase::PUBLISHING);
    if (state_.compare_exchange_weak(observed, publishing, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      break;
    }
  }

  // PUBLISHING owns the deferred slot and the caller-buffer lifetime. The timeout path
  // can no longer cancel or reuse either one after this claim succeeds.
  WriteInfoBlock info = deferred_info_;
  observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    ASSERT_FROM_CALLBACK(CurrentPhase(observed) == Phase::PUBLISHING, in_isr);
    ASSERT_FROM_CALLBACK(Active(observed) == ActiveState::NONE, in_isr);

    const bool has_capacity =
        queue_info_->EmptySize() >= 1U && queue_data_->EmptySize() >= info.data.size_;
    if (has_capacity)
    {
      UNUSED(PublishOwned(info.data, info.op, false, in_isr, true));
      return;
    }

    if (HasPublishTimeout(observed))
    {
      uint32_t cancelled = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
      cancelled = WithResult(cancelled | HANDOFF, ErrorCode::TIMEOUT);
      if (state_.compare_exchange_weak(observed, cancelled, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        info.op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
      continue;
    }

    if (HasKick(observed))
    {
      // Consume this progress carrier without releasing the deferred slot, then sample
      // capacity again. A concurrent carrier makes this CAS fail instead of being lost.
      const uint32_t retry = observed & ~KICK;
      if (state_.compare_exchange_weak(observed, retry, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        observed = state_.load(std::memory_order_acquire);
      }
      continue;
    }

    const uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::WAITING));
    if (!state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      continue;
    }

    return;
  }
}

void WritePort::FailPendingAndResetForBackendTeardown(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  uint32_t observed = state_.load(std::memory_order_acquire);
  bool has_deferred = false;
  WriteOperation deferred_operation;

  while (true)
  {
    const Phase phase = CurrentPhase(observed);
    if (phase == Phase::OWNER || phase == Phase::PUBLISHING)
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    if (Active(observed) == ActiveState::CLAIMED)
    {
      return;
    }

    ASSERT_FROM_CALLBACK(phase == Phase::FREE || phase == Phase::WAITING, in_isr);
    const uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::OWNER));
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      has_deferred = phase == Phase::WAITING;
      if (has_deferred)
      {
        deferred_operation = deferred_info_.op;
      }
      break;
    }
  }

  WriteInfoBlock info{};
  WriteInfoBlock block_info{};
  bool has_block_info = false;

  queue_data_->Reset();
  while (queue_info_->Pop(info) == ErrorCode::OK)
  {
    if (info.op.type == WriteOperation::OperationType::BLOCK)
    {
      DEV_ASSERT_FROM_CALLBACK(!has_block_info, in_isr);
      if (!has_block_info)
      {
        block_info = info;
        has_block_info = true;
      }
      continue;
    }

    info.op.UpdateStatus(in_isr, reason);
  }

  observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    ASSERT_FROM_CALLBACK(CurrentPhase(observed) == Phase::OWNER, in_isr);
    const ActiveState active = Active(observed);
    uint32_t desired = WithoutPublicationFlags(WithPhase(observed, Phase::FREE));
    bool post_block = false;

    if (active == ActiveState::WAITING)
    {
      DEV_ASSERT_FROM_CALLBACK(!HasHandoff(observed), in_isr);
      DEV_ASSERT_FROM_CALLBACK(has_block_info && !has_deferred, in_isr);
      if (!has_block_info || has_deferred)
      {
        return;
      }
      desired = WithResult(WithActive(desired, ActiveState::CLAIMED), reason);
      post_block = true;
    }
    else if (active == ActiveState::DETACHED)
    {
      DEV_ASSERT_FROM_CALLBACK(has_block_info, in_isr);
      desired = WithActive(desired, ActiveState::NONE);
    }
    else if (active == ActiveState::NONE)
    {
      DEV_ASSERT_FROM_CALLBACK(!has_block_info, in_isr);
      desired = WithActive(desired, ActiveState::NONE);
    }
    else
    {
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
    }

    desired = WithResult(desired,
                         (has_deferred || post_block)
                             ? reason
                             : (HasHandoff(observed) ? Result(observed) : ErrorCode::OK));
    if (has_deferred)
    {
      DEV_ASSERT_FROM_CALLBACK(!HasHandoff(observed), in_isr);
      desired |= HANDOFF;
    }
    if (!state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      continue;
    }

    if (post_block)
    {
      block_info.op.data.sem_info.sem->PostFromCallback(in_isr);
    }
    else if (has_deferred)
    {
      deferred_operation.data.sem_info.sem->PostFromCallback(in_isr);
    }
    return;
  }
}
