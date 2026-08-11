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
    if (BaseState(state) == BusyState::PENDING || HasEvent(state))
    {
      return state;
    }
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
      if (BaseState(observed) == BusyState::PENDING)
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

  // Publish a queue-fed request from the idle endpoint state. The acquire side of this
  // RMW consumes any producer event recorded before the request became PENDING.
  static bool PublishRequest(std::atomic<BusyState>& state, bool& had_event)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      if (BaseState(observed) != BusyState::IDLE)
      {
        return false;
      }

      had_event = HasEvent(observed);
      if (state.compare_exchange_weak(observed, BusyState::PENDING,
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

  static void PublishPending(std::atomic<BusyState>& state, BusyState claimed,
                             bool& had_event)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      if (observed == claimed || observed == EventState(claimed))
      {
        had_event = had_event || HasEvent(observed);
        if (state.compare_exchange_weak(observed, BusyState::PENDING,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
        {
          return;
        }
        continue;
      }
      ASSERT(false);
      return;
    }
  }

  // Claim a BLOCK completion while preserving an event recorded during the short claim
  // window. Once CLAIMED has been acquired, completion wins over timeout.
  static void PublishBlockClaim(std::atomic<BusyState>& state)
  {
    while (true)
    {
      BusyState observed = state.load(std::memory_order_acquire);
      BusyState target = BusyState::BLOCK_CLAIMED;
      if (observed == EventState(BusyState::CLAIMED))
      {
        target = static_cast<BusyState>(static_cast<uint32_t>(target) | EVENT_MASK);
      }
      else if (observed != BusyState::CLAIMED)
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

bool ReadPort::Readable() { return queue_data_ != nullptr; }

void ReadPort::Finish(bool in_isr, ErrorCode ans, ReadInfoBlock& info)
{
  if (info.op.type == ReadOperation::OperationType::BLOCK)
  {
    const auto state = busy_.load(std::memory_order_acquire);
    ASSERT(StateMachine::BaseState(state) == BusyState::BLOCK_CLAIMED);
    block_result_ = ans;
    info.op.data.sem_info.sem->PostFromCallback(in_isr);
    return;
  }

  auto operation = info.op;
  StateMachine::PublishAfterConsumer(busy_, BusyState::CLAIMED);
  operation.UpdateStatus(in_isr, ans);
}

void ReadPort::MarkAsRunning(ReadInfoBlock& info) { info.op.MarkAsRunning(); }

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(op.type != ReadOperation::OperationType::BLOCK || !in_isr,
                        in_isr);

  if (!Readable())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const BusyState observed = busy_.load(std::memory_order_acquire);
  if (StateMachine::BaseState(observed) != BusyState::IDLE)
  {
    return ErrorCode::BUSY;
  }

  info_ = ReadInfoBlock{data, op};
  op.MarkAsRunning();

  bool had_event = false;
  if (!StateMachine::PublishRequest(busy_, had_event))
  {
    // Ordinary Read/Clear calls are one caller-serialized SPSC consumer. Under that
    // contract only an EVENT transition can race this publication.
    return ErrorCode::BUSY;
  }

  const size_t queued_size = queue_data_->Size();
  if (had_event || (queued_size != 0U && queued_size >= data.size_))
  {
    ProcessPendingReads(in_isr);
  }
  if (op.type != ReadOperation::OperationType::BLOCK)
  {
    return ErrorCode::OK;
  }

  ASSERT(!in_isr);
  const auto wait_ans = op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
  if (wait_ans == ErrorCode::OK)
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
    BusyState expected = BusyState::PENDING;
    if (busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      return ErrorCode::TIMEOUT;
    }

    if (StateMachine::BaseState(expected) == BusyState::BLOCK_CLAIMED)
    {
      const auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
      REQUIRE(finish_wait_ans == ErrorCode::OK);
      const ErrorCode result = block_result_;
      StateMachine::ReleaseBlockClaim(busy_);
      return result;
    }

    if (StateMachine::BaseState(expected) == BusyState::CLAIMED)
    {
      // Preserve the historical completion-wins boundary without spinning at a
      // higher priority than the claimed path. A successful wait is its completion
      // post; a short timeout means it may have returned to PENDING.
      const auto claim_wait_ans = op.data.sem_info.sem->Wait(1U);
      if (claim_wait_ans == ErrorCode::OK)
      {
        REQUIRE(StateMachine::BaseState(busy_.load(std::memory_order_acquire)) ==
                BusyState::BLOCK_CLAIMED);
        const ErrorCode result = block_result_;
        StateMachine::ReleaseBlockClaim(busy_);
        return result;
      }
      REQUIRE(claim_wait_ans == ErrorCode::TIMEOUT);
      continue;
    }

    // Timeout returns only when it wins PENDING -> IDLE. Once completion owns the
    // request, wait for its result instead of changing the operation outcome.
    REQUIRE(false);
  }
}

void ReadPort::ProcessPendingReads(bool in_isr)
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

    ReadInfoBlock local_info = info_;
    const size_t required_size = local_info.data.size_;
    const size_t size = queue_data_->Size();
    if (size == 0U || size < required_size)
    {
      // A producer that ran before this release observed CLAIMED and returned. Re-read
      // readiness after publishing PENDING; a later producer observes PENDING and claims
      // completion itself.
      bool had_event = false;
      StateMachine::PublishPending(busy_, BusyState::CLAIMED, had_event);
      const size_t queued_size = queue_data_->Size();
      if (had_event || (queued_size != 0U && queued_size >= required_size))
      {
        continue;
      }
      return;
    }

    if (local_info.op.type == ReadOperation::OperationType::BLOCK)
    {
      StateMachine::PublishBlockClaim(busy_);
    }

    if (local_info.data.size_ > 0)
    {
      const auto ans = queue_data_->PopBatch(
          reinterpret_cast<uint8_t*>(local_info.data.addr_), local_info.data.size_);
      ASSERT(ans == ErrorCode::OK);
      Finish(in_isr, ErrorCode::OK, local_info);
      OnRxDequeue(in_isr);
    }
    else
    {
      Finish(in_isr, ErrorCode::OK, local_info);
    }
    return;
  }
}

ErrorCode ReadPort::ClearQueuedData(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    BusyState observed = busy_.load(std::memory_order_acquire);
    if (StateMachine::BaseState(observed) != BusyState::IDLE)
    {
      return ErrorCode::BUSY;
    }
    if (busy_.compare_exchange_weak(observed, BusyState::CLAIMED,
                                    std::memory_order_acq_rel, std::memory_order_acquire))
    {
      break;
    }
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

void ReadPort::FailAndClearAll(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  auto state = busy_.load(std::memory_order_acquire);
  auto base = StateMachine::BaseState(state);
  if (base == BusyState::CLAIMED)
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return;
  }

  queue_data_->Reset();

  if (base == BusyState::PENDING)
  {
    if (info_.op.type == ReadOperation::OperationType::BLOCK)
    {
      BusyState expected = BusyState::PENDING;
      if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
      {
        Finish(in_isr, reason, info_);
        return;
      }
      state = expected;
      base = StateMachine::BaseState(state);
      if (base == BusyState::CLAIMED)
      {
        DEV_ASSERT_FROM_CALLBACK(false, in_isr);
        return;
      }
    }
    else
    {
      auto operation = info_.op;
      BusyState expected = BusyState::PENDING;
      if (!busy_.compare_exchange_strong(expected, BusyState::IDLE,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        return;
      }
      operation.UpdateStatus(in_isr, reason);
      return;
    }
  }

  if (StateMachine::BaseState(state) == BusyState::BLOCK_CLAIMED)
  {
    return;
  }

  block_result_ = ErrorCode::OK;
  busy_.store(BusyState::IDLE, std::memory_order_release);
}
