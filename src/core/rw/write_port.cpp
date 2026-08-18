#include "write_port.hpp"

#include <new>

using namespace LibXR;

template class LibXR::SPSCQueue<uint8_t>;

#ifdef LIBXR_TEST_BUILD
std::atomic<void (*)()> WritePort::stream_publication_hook_{nullptr};

void WritePortTestAccess::SetStreamPublicationHook(void (*hook)())
{
  WritePort::stream_publication_hook_.store(hook, std::memory_order_release);
}
#endif

WritePort::WritePort(size_t queue_size, size_t buffer_size)
    : queue_requests_(new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                          SPSCQueue<Request>(queue_size)),
      queue_data_(buffer_size > 0U ? new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
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

size_t WritePort::Capacity() const
{
  return queue_data_ == nullptr ? 0U : queue_data_->MaxSize();
}

bool WritePort::Writable() { return write_fun_ != nullptr; }

WritePort& WritePort::operator=(WriteFun fun)
{
  write_fun_ = fun;
  return *this;
}

bool WritePort::TryAcquireOwner(bool allow_detached)
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::FREE)
  {
    const ActiveState active = Active(observed);
    if (active != ActiveState::NONE &&
        (!allow_detached || active != ActiveState::DETACHED))
    {
      return false;
    }

    const uint32_t desired = WithPhase(observed & ~KICK, Phase::OWNER);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      return true;
    }
  }
  return false;
}

void WritePort::ReleaseOwner(bool in_isr)
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::OWNER)
  {
    const ActiveState active = Active(observed);
    if (active != ActiveState::NONE && active != ActiveState::DETACHED)
    {
      break;
    }

    const uint32_t desired = WithPhase(observed & ~KICK, Phase::FREE);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                     std::memory_order_relaxed))
    {
      return;
    }
  }
  ASSERT_FROM_CALLBACK(false, in_isr);
}

void WritePort::NotifyBackend(bool in_isr)
{
  if (write_fun_ != nullptr)
  {
    write_fun_(*this, in_isr);
  }
}

WritePort::WriteQueue::~WriteQueue() noexcept { port_.SettleWriteQueue(*this); }

bool WritePort::WriteQueue::FailFront(ErrorCode error)
{
  REQUIRE_FROM_CALLBACK(error < ErrorCode::OK, in_isr_);
  if (front_size == 0U || popped_size_ != 0U || failed_front_)
  {
    return false;
  }

  REQUIRE_FROM_CALLBACK(
      port_.MutableDataQueue().PopBatch(nullptr, front_size) == ErrorCode::OK, in_isr_);
  popped_size_ = front_size;
  front_result_ = error;
  failed_front_ = true;
  return true;
}

WritePort::WriteQueue WritePort::GetWriteQueue(bool in_isr)
{
  const size_t published = published_request_count_.load(std::memory_order_acquire);
  if (published == 0U)
  {
    REQUIRE_FROM_CALLBACK(front_remaining_ == 0U, in_isr);
    return WriteQueue(*this, in_isr, 0U, 0U);
  }

  Request requests[2]{};
  if (published == 1U)
  {
    REQUIRE_FROM_CALLBACK(queue_requests_->Peek(requests[0]) == ErrorCode::OK, in_isr);
  }
  else
  {
    REQUIRE_FROM_CALLBACK(queue_requests_->PeekBatch(requests, 2U) == ErrorCode::OK,
                          in_isr);
  }

  if (front_remaining_ == 0U)
  {
    REQUIRE_FROM_CALLBACK(requests[0].size != 0U, in_isr);
    front_remaining_ = requests[0].size;
  }
  else
  {
    REQUIRE_FROM_CALLBACK(front_remaining_ <= requests[0].size, in_isr);
  }

  return WriteQueue(*this, in_isr, front_remaining_,
                    published > 1U ? requests[1].size : 0U);
}

void WritePort::CompleteRequest(Request& request, ErrorCode result, bool in_isr)
{
  if (request.op.type != WriteOperation::OperationType::BLOCK)
  {
    request.op.UpdateStatus(in_isr, result);
    return;
  }

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    const ActiveState active = Active(observed);
    if (active == ActiveState::WAITING)
    {
      block_result_ = result;
      const uint32_t desired = WithActive(observed, ActiveState::CLAIMED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        request.op.data.sem_info.sem->PostFromCallback(in_isr);
        return;
      }
      continue;
    }

    if (active == ActiveState::DETACHED)
    {
      const uint32_t desired = WithActive(observed, ActiveState::NONE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return;
      }
      continue;
    }

    REQUIRE_FROM_CALLBACK(false, in_isr);
  }
}

void WritePort::SettleWriteQueue(WriteQueue& queue) noexcept
{
  if (queue.popped_size_ == 0U)
  {
    return;
  }

  REQUIRE_FROM_CALLBACK(queue.popped_size_ <= queue.front_size + queue.next_size,
                        queue.in_isr_);

  Request completed[2]{};
  ErrorCode results[2] = {ErrorCode::OK, ErrorCode::OK};
  size_t completed_count = 0U;
  size_t remaining = queue.popped_size_;
  size_t request_remaining = queue.front_size;
  while (remaining != 0U)
  {
    REQUIRE_FROM_CALLBACK(request_remaining != 0U, queue.in_isr_);
    Request request{};

    if (remaining < request_remaining)
    {
      front_remaining_ = request_remaining - remaining;
      remaining = 0U;
      break;
    }

    remaining -= request_remaining;
    front_remaining_ = 0U;
    REQUIRE_FROM_CALLBACK(completed_count < 2U, queue.in_isr_);
    REQUIRE_FROM_CALLBACK(queue_requests_->Pop(request) == ErrorCode::OK, queue.in_isr_);
    completed[completed_count] = request;
    if (completed_count == 0U && queue.failed_front_)
    {
      results[completed_count] = queue.front_result_;
    }
    completed_count++;
    request_remaining = queue.next_size;
  }

  if (completed_count != 0U)
  {
    const size_t published =
        published_request_count_.fetch_sub(completed_count, std::memory_order_acq_rel);
    REQUIRE_FROM_CALLBACK(published >= completed_count, queue.in_isr_);
  }

  for (size_t i = 0U; i < completed_count; ++i)
  {
    CompleteRequest(completed[i], results[i], queue.in_isr_);
  }
  ProcessPendingWrites(queue.in_isr_);
}

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

  if (TryAcquireOwner(op.type != WriteOperation::OperationType::BLOCK))
  {
    const bool has_capacity =
        queue_requests_->EmptySize() >= 1U && queue_data_->EmptySize() >= data.size_;
    if (has_capacity)
    {
      CopyAndPublishOwned(data, op, in_isr);
      return op.type == WriteOperation::OperationType::BLOCK ? WaitForBlock(op)
                                                             : ErrorCode::OK;
    }

    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return DeferBlock(data, op, true);
    }

    ReleaseOwner(in_isr);
    NotifyBackend(in_isr);
    return ErrorCode::FULL;
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    return DeferBlock(data, op, false);
  }

  return ErrorCode::BUSY;
}

void WritePort::CopyAndPublishOwned(ConstRawData data, WriteOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(
      MutableDataQueue().PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                   data.size_) == ErrorCode::OK,
      in_isr);
  PublishOwned(data.size_, op, in_isr);
}

void WritePort::PublishOwned(size_t size, WriteOperation& op, bool in_isr,
                             Stream* releasing_stream)
{
  op.MarkAsRunning();
  REQUIRE_FROM_CALLBACK(queue_requests_->Push(Request{size, op}) == ErrorCode::OK,
                        in_isr);

  const bool block = op.type == WriteOperation::OperationType::BLOCK;
  bool notify_copy_waiter = false;

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::OWNER)
  {
    uint32_t desired = observed & ~KICK;
    if (!block)
    {
      REQUIRE_FROM_CALLBACK(Active(observed) == ActiveState::NONE ||
                                Active(observed) == ActiveState::DETACHED,
                            in_isr);
      desired = WithPhase(desired, Phase::FREE);
    }
    else if (Active(observed) == ActiveState::NONE)
    {
      desired = WithActive(WithPhase(desired, Phase::FREE), ActiveState::WAITING);
    }
    else
    {
      REQUIRE_FROM_CALLBACK(Active(observed) == ActiveState::DETACHED, in_isr);
      desired = WithPhase(desired, Phase::FREE);
      notify_copy_waiter = true;
    }

    if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                     std::memory_order_relaxed))
    {
      break;
    }
  }
  REQUIRE_FROM_CALLBACK(CurrentPhase(observed) == Phase::OWNER, in_isr);

  // A deferred timeout may return as soon as the full caller buffer belongs to the port.
  // The request is already private and marked DETACHED; later backend completion will not
  // post this semaphore a second time.
  if (notify_copy_waiter)
  {
    op.data.sem_info.sem->PostFromCallback(in_isr);
  }

  // Keep the Stream in SUBMITTING until the Port owner transition and request-count
  // publication are both visible. An older completion may race this boundary; it must
  // observe SUBMITTING and be rejected rather than reacquiring the Stream before the
  // request is discoverable by the backend.
  published_request_count_.fetch_add(1U, std::memory_order_release);
  if (releasing_stream != nullptr)
  {
#ifdef LIBXR_TEST_BUILD
    // Test-only interleaving point for the Stream/Port handoff invariant.
    if (auto hook = stream_publication_hook_.load(std::memory_order_acquire); hook != nullptr)
    {
      hook();
    }
#endif
    releasing_stream->state_.store(Stream::StreamState::RELEASED,
                                   std::memory_order_release);
  }
  NotifyBackend(in_isr);
}

ErrorCode WritePort::DeferBlock(ConstRawData data, WriteOperation& op, bool owns_port)
{
  if (!owns_port && !TryAcquireOwner(true))
  {
    return ErrorCode::BUSY;
  }

  deferred_request_ = DeferredRequest{data, op};

  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::OWNER)
  {
    const ActiveState active = Active(observed);
    REQUIRE(active == ActiveState::NONE || active == ActiveState::DETACHED);
    const uint32_t desired = WithPhase(observed, Phase::WAITING);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                     std::memory_order_relaxed))
    {
      break;
    }
  }
  REQUIRE(CurrentPhase(observed) == Phase::OWNER);

  // Close progress that happened before deferred_request_ became readable.
  ProcessPendingWrites(false);
  return WaitForBlock(op);
}

void WritePort::ReleaseBlockClaim()
{
  uint32_t observed = state_.load(std::memory_order_acquire);
  while (CurrentPhase(observed) == Phase::FREE &&
         Active(observed) == ActiveState::CLAIMED)
  {
    const uint32_t desired = WithActive(observed & ~KICK, ActiveState::NONE);
    if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                     std::memory_order_relaxed))
    {
      NotifyBackend(false);
      return;
    }
  }
  REQUIRE(false);
}

ErrorCode WritePort::WaitForBlock(WriteOperation& op)
{
  ErrorCode wait_result = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
  const ErrorCode original_wait_result = wait_result;

  while (true)
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    const Phase phase = CurrentPhase(observed);
    const ActiveState active = Active(observed);

    if (wait_result == ErrorCode::OK)
    {
      REQUIRE(phase == Phase::FREE && active == ActiveState::CLAIMED);
      const ErrorCode result = block_result_;
      ReleaseBlockClaim();
      return result;
    }

    if (active == ActiveState::CLAIMED)
    {
      do
      {
        wait_result = op.data.sem_info.sem->Wait(UINT32_MAX);
      } while (wait_result != ErrorCode::OK);
      continue;
    }

    if (phase == Phase::FREE && active == ActiveState::WAITING)
    {
      const uint32_t desired = WithActive(observed, ActiveState::DETACHED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return wait_result;
      }
      continue;
    }

    if (phase == Phase::WAITING &&
        (active == ActiveState::NONE || active == ActiveState::DETACHED))
    {
      const uint32_t desired = WithPhase(observed & ~KICK, Phase::FREE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        NotifyBackend(false);
        return wait_result;
      }
      continue;
    }

    if (phase == Phase::OWNER && active == ActiveState::NONE)
    {
      const uint32_t desired = WithActive(observed, ActiveState::DETACHED);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        // The publisher owns the caller buffer. It posts exactly once after the complete
        // copy and before exposing credit to the backend.
        do
        {
          wait_result = op.data.sem_info.sem->Wait(UINT32_MAX);
        } while (wait_result != ErrorCode::OK);
        return original_wait_result;
      }
      continue;
    }

    REQUIRE(false);
  }
}

void WritePort::ProcessPendingWrites(bool in_isr)
{
  ASSERT(queue_requests_ != nullptr);
  ASSERT(queue_data_ != nullptr);

  // Every progress source contributes a fresh RMW. Besides retaining KICK across an
  // OWNER capacity check, this carries the latest queue-head release to a waiter even
  // when an ordinary load could still observe an older FREE state.
  uint32_t observed = state_.fetch_or(KICK, std::memory_order_acq_rel) | KICK;
  while (true)
  {
    const Phase phase = CurrentPhase(observed);
    if (phase == Phase::FREE)
    {
      return;
    }

    if (phase == Phase::OWNER)
    {
      return;
    }

    REQUIRE_FROM_CALLBACK(phase == Phase::WAITING, in_isr);
    if (Active(observed) != ActiveState::NONE)
    {
      return;
    }

    // Progress observed before this claim is covered by the following capacity check.
    // Progress after the claim sees OWNER and leaves KICK for the return-to-WAITING CAS.
    const uint32_t owner = WithPhase(observed & ~KICK, Phase::OWNER);
    if (state_.compare_exchange_weak(observed, owner, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      break;
    }
  }

  // OWNER makes the deferred slot and caller-buffer lifetime stable. A timeout that
  // changes ActiveState to DETACHED now waits for this owner to publish or cancel.
  DeferredRequest request = deferred_request_;
  observed = state_.load(std::memory_order_acquire);
  while (true)
  {
    REQUIRE_FROM_CALLBACK(CurrentPhase(observed) == Phase::OWNER, in_isr);
    const ActiveState active = Active(observed);
    REQUIRE_FROM_CALLBACK(active == ActiveState::NONE || active == ActiveState::DETACHED,
                          in_isr);

    if (HasKick(observed))
    {
      const uint32_t retry = observed & ~KICK;
      if (!state_.compare_exchange_weak(observed, retry, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        continue;
      }
      observed = retry;
    }

    const bool has_capacity = queue_requests_->EmptySize() >= 1U &&
                              queue_data_->EmptySize() >= request.data.size_;
    if (has_capacity)
    {
      CopyAndPublishOwned(request.data, request.op, in_isr);
      return;
    }

    if (Active(observed) == ActiveState::DETACHED)
    {
      const uint32_t cancelled =
          WithActive(WithPhase(observed & ~KICK, Phase::FREE), ActiveState::NONE);
      if (!state_.compare_exchange_weak(observed, cancelled, std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        continue;
      }
      request.op.data.sem_info.sem->PostFromCallback(in_isr);
      NotifyBackend(in_isr);
      return;
    }

    const uint32_t waiting = WithPhase(observed, Phase::WAITING);
    if (state_.compare_exchange_weak(observed, waiting, std::memory_order_release,
                                     std::memory_order_relaxed))
    {
      return;
    }
  }
}
