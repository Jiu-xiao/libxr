#include "read_port.hpp"

#include <new>

using namespace LibXR;

struct ReadPort::StateMachine
{
  using BusyState = ReadPort::BusyState;
  static constexpr uint32_t EVENT_MASK = static_cast<uint32_t>(BusyState::EVENT);

  static BusyState BaseState(BusyState state)
  {
    return static_cast<BusyState>(static_cast<uint32_t>(state) & ~EVENT_MASK);
  }

  static bool HasEvent(BusyState state)
  {
    return (static_cast<uint32_t>(state) & EVENT_MASK) != 0U;
  }

  static BusyState EventState(BusyState state)
  {
    return static_cast<BusyState>(static_cast<uint32_t>(state) | EVENT_MASK);
  }

  // A producer that cannot claim PENDING must leave a carrier in the same atomic
  // modification order as the claimed path's release. Otherwise the consumer may legally
  // observe an old queue tail and return with queued data stranded.
  enum class EventRecordResult : uint8_t
  {
    RECORDED,
    PENDING,
  };

  static EventRecordResult RecordEvent(std::atomic<BusyState>& state)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      if (observed == BusyState::PENDING)
      {
        return EventRecordResult::PENDING;
      }
      const BusyState marked = EventState(observed);
      // A successful same-value CAS is still an RMW. Do not coalesce this away:
      // each producer commit needs a fresh modification-order carrier for its queue
      // writes.
      if (state.compare_exchange_weak(observed, marked, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return EventRecordResult::RECORDED;
      }
    }
  }

  // Claim request publication before info_ or its operation is modified.
  static bool ClaimIdle(std::atomic<BusyState>& state)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      if (BaseState(observed) != BusyState::IDLE)
      {
        return false;
      }

      if (state.compare_exchange_weak(observed, BusyState::CLAIMED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return true;
      }
    }
  }

  // Release a CLAIMED path while preserving an event recorded by a producer that raced
  // with the consumer.
  static void PublishAfterConsumer(std::atomic<BusyState>& state, BusyState owner)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      BusyState target = BusyState::IDLE;
      if (observed == EventState(owner))
      {
        target = BusyState::EVENT;
      }
      else if (observed != owner)
      {
        ASSERT(false);
        return;
      }

      if (state.compare_exchange_weak(observed, target, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return;
      }
    }
  }

  // Publish terminal BLOCK ownership while preserving producer progress.
  static void PublishBlockClaim(std::atomic<BusyState>& state)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      const BusyState base = BaseState(observed);
      if (base != BusyState::CLAIMED && base != BusyState::CLAIMED_WITH_WAITER)
      {
        ASSERT(false);
        return;
      }
      BusyState target = BusyState::BLOCK_CLAIMED;
      if (HasEvent(observed))
      {
        target = EventState(target);
      }

      if (state.compare_exchange_weak(observed, target, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return;
      }
    }
  }

  static void ReleaseBlockClaim(std::atomic<BusyState>& state)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      BusyState target = BusyState::IDLE;
      if (observed == static_cast<BusyState>(
                          static_cast<uint32_t>(BusyState::BLOCK_CLAIMED) | EVENT_MASK))
      {
        target = BusyState::EVENT;
      }
      else if (observed != BusyState::BLOCK_CLAIMED)
      {
        ASSERT(false);
        return;
      }

      if (state.compare_exchange_weak(observed, target, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return;
      }
    }
  }
};

ReadPort::ReadPort(size_t buffer_size)
    : queue_data_(buffer_size > 0 ? new (std::align_val_t(LibXR::CONCURRENCY_ALIGNMENT))
                                        SPSCQueue<uint8_t>(buffer_size)
                                  : nullptr)
{
}

ReadPort::ReadQueue::~ReadQueue() { ASSERT_FROM_CALLBACK(!dirty_, in_isr_); }

ErrorCode ReadPort::ReadQueue::PushBatch(const uint8_t* data, size_t size)
{
  if (!CanPush())
  {
    return ErrorCode::STATE_ERR;
  }

  const ErrorCode result = port_->queue_data_->PushBatch(data, size);
  RecordPushResult(size, result);
  return result;
}

size_t ReadPort::ReadQueue::EmptySize() const
{
  DEV_ASSERT_FROM_CALLBACK(!finished_, in_isr_);
  return finished_ ? 0U : port_->queue_data_->EmptySize();
}

size_t ReadPort::ReadQueue::Capacity() const
{
  DEV_ASSERT_FROM_CALLBACK(!finished_, in_isr_);
  return finished_ ? 0U : port_->queue_data_->MaxSize();
}

void ReadPort::ReadQueue::Publish()
{
  DEV_ASSERT_FROM_CALLBACK(!finished_, in_isr_);
  if (finished_)
  {
    return;
  }

  finished_ = true;
  const bool publish = dirty_;
  dirty_ = false;
  if (publish)
  {
    port_->PublishProduced(in_isr_);
  }
}

ReadPort::ReadQueue ReadPort::GetReadQueue(bool in_isr)
{
  ASSERT_FROM_CALLBACK(queue_data_ != nullptr, in_isr);
  return ReadQueue(*this, in_isr);
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

size_t ReadPort::Capacity() const
{
  return queue_data_ == nullptr ? 0U : queue_data_->MaxSize();
}

bool ReadPort::Readable() { return queue_data_ != nullptr; }

bool ReadPort::TryCompleteClaimedRead(bool in_isr, bool signal_block_completion)
{
  const Request local_info = info_;
  const size_t required_size = local_info.data.size_;

  while (true)
  {
    const size_t queued_size = queue_data_->Size();
    if (queued_size != 0U && queued_size >= required_size)
    {
      const bool block = local_info.op.type == ReadOperation::OperationType::BLOCK;
      if (required_size > 0U)
      {
        const auto ans = queue_data_->PopBatch(
            reinterpret_cast<uint8_t*>(local_info.data.addr_), required_size);
        ASSERT(ans == ErrorCode::OK);
      }

      if (block)
      {
        if (!signal_block_completion)
        {
          StateMachine::PublishAfterConsumer(busy_, BusyState::CLAIMED);
        }
        else
        {
          block_result_ = ErrorCode::OK;
          StateMachine::PublishBlockClaim(busy_);
        }
      }
      else
      {
        StateMachine::PublishAfterConsumer(busy_, BusyState::CLAIMED);
      }

      if (required_size > 0U)
      {
        OnRxDequeue(in_isr);
      }

      if (!block)
      {
        auto operation = local_info.op;
        operation.UpdateStatus(in_isr, ErrorCode::OK);
      }
      else if (signal_block_completion)
      {
        local_info.op.data.sem_info.sem->PostFromCallback(in_isr);
      }
      return true;
    }

    BusyState observed = busy_.load(std::memory_order_acquire);
    const BusyState base = StateMachine::BaseState(observed);
    ASSERT(base == BusyState::CLAIMED || base == BusyState::CLAIMED_WITH_WAITER);

    if (StateMachine::HasEvent(observed))
    {
      if (busy_.compare_exchange_weak(observed, base, std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        continue;
      }
      continue;
    }

    const BusyState desired =
        base == BusyState::CLAIMED ? BusyState::PENDING : BusyState::IDLE;
    if (!busy_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      continue;
    }

    if (base == BusyState::CLAIMED_WITH_WAITER)
    {
      local_info.op.data.sem_info.sem->PostFromCallback(in_isr);
    }
    return false;
  }
}

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(op.type != ReadOperation::OperationType::BLOCK || !in_isr,
                        in_isr);

  if (!Readable())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (data.size_ > queue_data_->MaxSize())
  {
    return ErrorCode::SIZE_ERR;
  }

  if (data.size_ > 0U && data.addr_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  if (!StateMachine::ClaimIdle(busy_))
  {
    return ErrorCode::BUSY;
  }

  info_ = Request{data, op};
  op.MarkAsRunning();

  if (TryCompleteClaimedRead(in_isr, false))
  {
    return ErrorCode::OK;
  }
  if (op.type != ReadOperation::OperationType::BLOCK)
  {
    return ErrorCode::OK;
  }

  ASSERT(!in_isr);
  const ErrorCode wait_error = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
  if (wait_error == ErrorCode::OK)
  {
#ifdef LIBXR_DEBUG_BUILD
    const auto state = busy_.load(std::memory_order_acquire);
    ASSERT(StateMachine::BaseState(state) == BusyState::BLOCK_CLAIMED);
#endif
    const ErrorCode result = block_result_;
    StateMachine::ReleaseBlockClaim(busy_);
    return result;
  }

  while (true)
  {
    BusyState observed = busy_.load(std::memory_order_acquire);
    const BusyState base = StateMachine::BaseState(observed);

    if (observed == BusyState::PENDING &&
        busy_.compare_exchange_weak(observed, BusyState::IDLE, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
    {
      return wait_error;
    }

    if (base == BusyState::CLAIMED)
    {
      BusyState desired = BusyState::CLAIMED_WITH_WAITER;
      if (StateMachine::HasEvent(observed))
      {
        desired = StateMachine::EventState(desired);
      }
      if (!busy_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        continue;
      }

      while (op.data.sem_info.sem->Wait(UINT32_MAX) != ErrorCode::OK)
      {
      }

      observed = busy_.load(std::memory_order_acquire);
      const BusyState handoff_base = StateMachine::BaseState(observed);
      if (handoff_base == BusyState::BLOCK_CLAIMED)
      {
        const ErrorCode result = block_result_;
        StateMachine::ReleaseBlockClaim(busy_);
        return result;
      }

      if (handoff_base == BusyState::IDLE)
      {
        return wait_error;
      }
      REQUIRE(false);
    }

    if (base == BusyState::BLOCK_CLAIMED)
    {
      while (op.data.sem_info.sem->Wait(UINT32_MAX) != ErrorCode::OK)
      {
      }
      const ErrorCode result = block_result_;
      StateMachine::ReleaseBlockClaim(busy_);
      return result;
    }

    REQUIRE(base == BusyState::PENDING);
  }
}

void ReadPort::PublishProduced(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    BusyState expected = BusyState::PENDING;
    if (!busy_.compare_exchange_strong(expected, BusyState::CLAIMED,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
      const BusyState base = StateMachine::BaseState(expected);
      if (base == BusyState::IDLE && queue_data_->Size() == 0U)
      {
        return;
      }
      if (StateMachine::RecordEvent(busy_) == StateMachine::EventRecordResult::PENDING)
      {
        continue;
      }
      return;
    }

    (void)TryCompleteClaimedRead(in_isr, true);
    return;
  }
}

ErrorCode ReadPort::ClearQueuedData(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  if (!StateMachine::ClaimIdle(busy_))
  {
    return ErrorCode::BUSY;
  }

  const size_t queued_size = queue_data_->Size();
  if (queued_size > 0)
  {
    const ErrorCode pop_ans = queue_data_->PopBatch(nullptr, queued_size);
    ASSERT(pop_ans == ErrorCode::OK);
    StateMachine::PublishAfterConsumer(busy_, BusyState::CLAIMED);
    OnRxDequeue(in_isr);
  }
  else
  {
    StateMachine::PublishAfterConsumer(busy_, BusyState::CLAIMED);
  }
  return ErrorCode::OK;
}
