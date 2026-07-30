#include "read_port.hpp"

#include <new>

using namespace LibXR;

namespace
{
using BusyState = ReadPort::BusyState;
constexpr uint32_t EVENT_MASK = static_cast<uint32_t>(BusyState::EVENT);

BusyState EventState(BusyState state)
{
  if ((static_cast<uint32_t>(state) & ~EVENT_MASK) ==
      static_cast<uint32_t>(BusyState::PENDING))
  {
    return state;
  }

  return static_cast<BusyState>(static_cast<uint32_t>(state) | EVENT_MASK);
}

void RecordEvent(std::atomic<BusyState>& state)
{
  while (true)
  {
    BusyState observed = state.load(std::memory_order_acquire);
    const BusyState marked = EventState(observed);

    if (marked == observed)
    {
      return;
    }

    if (state.compare_exchange_weak(observed, marked, std::memory_order_acq_rel,
                                    std::memory_order_acquire))
    {
      return;
    }
  }
}

void PublishAfterConsumer(std::atomic<BusyState>& state, BusyState owner,
                          bool preserve_event = false)
{
  while (true)
  {
    BusyState observed = state.load(std::memory_order_acquire);
    BusyState target = preserve_event ? BusyState::EVENT : BusyState::IDLE;

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

bool PublishPending(std::atomic<BusyState>& state, BusyState owner)
{
  while (true)
  {
    BusyState observed = state.load(std::memory_order_acquire);
    const bool had_event = observed == EventState(owner);
    if (observed == owner || had_event)
    {
      if (state.compare_exchange_weak(observed, BusyState::PENDING,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
      {
        return had_event;
      }
      continue;
    }

    ASSERT(false);
    return false;
  }
}

void PublishBlockClaim(std::atomic<BusyState>& state)
{
  BusyState expected = BusyState::PROCESSING;
  if (state.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED,
                                    std::memory_order_acq_rel, std::memory_order_acquire))
  {
    return;
  }

  ASSERT(expected == BusyState::PROCESSING_EVENT);
  expected = BusyState::PROCESSING_EVENT;
  const bool claimed =
      state.compare_exchange_strong(expected, BusyState::BLOCK_CLAIMED_EVENT,
                                    std::memory_order_acq_rel, std::memory_order_acquire);
  ASSERT(claimed);
}

void ReleaseBlockClaim(std::atomic<BusyState>& state)
{
  while (true)
  {
    BusyState observed = state.load(std::memory_order_acquire);
    BusyState target = BusyState::IDLE;
    if (observed == BusyState::BLOCK_CLAIMED_EVENT)
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

void ReleaseDetached(std::atomic<BusyState>& state)
{
  while (true)
  {
    BusyState observed = state.load(std::memory_order_acquire);
    BusyState target = BusyState::IDLE;
    if (observed == BusyState::BLOCK_DETACHED_EVENT)
    {
      target = BusyState::EVENT;
    }
    else if (observed != BusyState::BLOCK_DETACHED)
    {
      if (observed == BusyState::EVENT)
      {
        return;
      }
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
}  // namespace

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
    const auto state = busy_.load(std::memory_order_acquire);
    ASSERT(state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_CLAIMED_EVENT);
    block_result_ = ans;
    info.op.data.sem_info.sem->PostFromCallback(in_isr);
    return;
  }

  auto operation = info.op;
  PublishAfterConsumer(busy_, BusyState::PROCESSING);
  operation.UpdateStatus(in_isr, ans);
}

void ReadPort::MarkAsRunning(ReadInfoBlock& info) { info.op.MarkAsRunning(); }

ErrorCode ReadPort::operator()(RawData data, ReadOperation& op, bool in_isr)
{
  if (!Readable())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  while (true)
  {
    const BusyState state = busy_.load(std::memory_order_acquire);
    if (state != BusyState::IDLE && state != BusyState::EVENT)
    {
      return ErrorCode::BUSY;
    }

    BusyState expected = state;
    if (!busy_.compare_exchange_weak(expected, BusyState::CLEARING,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      continue;
    }

    const size_t readable_size = queue_data_->Size();
    if (readable_size >= data.size_ && readable_size != 0)
    {
      if (data.size_ > 0)
      {
        const auto ans =
            queue_data_->PopBatch(reinterpret_cast<uint8_t*>(data.addr_), data.size_);
        ASSERT(ans == ErrorCode::OK);
        PublishAfterConsumer(busy_, BusyState::CLEARING);
        OnRxDequeue(in_isr);
      }
      else
      {
        PublishAfterConsumer(busy_, BusyState::CLEARING);
      }

      if (op.type != ReadOperation::OperationType::BLOCK)
      {
        op.UpdateStatus(in_isr, ErrorCode::OK);
      }
      return ErrorCode::OK;
    }

    info_ = ReadInfoBlock{data, op};
    op.MarkAsRunning();
    (void)PublishPending(busy_, BusyState::CLEARING);

    const auto ans = read_fun_(*this, in_isr);
    if (static_cast<int8_t>(ans) >= 0)
    {
      // A producer may have pushed while CLEARING suppressed its notification.
      // Re-check now that the request is visible as PENDING.
      ProcessPendingReads(in_isr);
      break;
    }

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

    if (expected == BusyState::BLOCK_DETACHED ||
        expected == BusyState::BLOCK_DETACHED_EVENT)
    {
      return ErrorCode::TIMEOUT;
    }
    if (expected == BusyState::IDLE || expected == BusyState::EVENT ||
        expected == BusyState::PROCESSING || expected == BusyState::PROCESSING_EVENT)
    {
      if (op.type != ReadOperation::OperationType::BLOCK)
      {
        return ErrorCode::OK;
      }
      break;
    }

    ASSERT(expected == BusyState::BLOCK_CLAIMED ||
           expected == BusyState::BLOCK_CLAIMED_EVENT);
    break;
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
    ASSERT(state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_CLAIMED_EVENT);
#endif
    ReleaseBlockClaim(busy_);
    return block_result_;
  }

  while (true)
  {
    BusyState expected = BusyState::PENDING;
    if (busy_.compare_exchange_strong(expected, BusyState::BLOCK_DETACHED,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
    {
      ReleaseDetached(busy_);
      return ErrorCode::TIMEOUT;
    }

    if (expected == BusyState::BLOCK_DETACHED ||
        expected == BusyState::BLOCK_DETACHED_EVENT)
    {
      ReleaseDetached(busy_);
      return ErrorCode::TIMEOUT;
    }

    if (expected == BusyState::BLOCK_CLAIMED ||
        expected == BusyState::BLOCK_CLAIMED_EVENT)
    {
      const auto finish_wait_ans = op.data.sem_info.sem->Wait(UINT32_MAX);
      UNUSED(finish_wait_ans);
      ASSERT(finish_wait_ans == ErrorCode::OK);
      ReleaseBlockClaim(busy_);
      return block_result_;
    }

    // PROCESSING is a short queue-claim window. Retry the cancellation CAS;
    // the completion path either returns to PENDING or posts the semaphore.
    ASSERT(expected == BusyState::PROCESSING || expected == BusyState::PROCESSING_EVENT);
  }
}

void ReadPort::ProcessPendingReads(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    const BusyState state = busy_.load(std::memory_order_acquire);

    if (state == BusyState::PENDING)
    {
      BusyState expected = BusyState::PENDING;
      if (!busy_.compare_exchange_strong(expected, BusyState::PROCESSING,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire))
      {
        continue;
      }

      const size_t size = queue_data_->Size();
      if (size == 0 || size < info_.data.size_)
      {
        const bool had_event =
            busy_.load(std::memory_order_acquire) == BusyState::PROCESSING_EVENT;
        const bool published_event = PublishPending(busy_, BusyState::PROCESSING);
        if (had_event || published_event)
        {
          continue;
        }
        return;
      }

      if (info_.op.type == ReadOperation::OperationType::BLOCK)
      {
        PublishBlockClaim(busy_);
      }

      if (info_.data.size_ > 0)
      {
        const auto ans = queue_data_->PopBatch(
            reinterpret_cast<uint8_t*>(info_.data.addr_), info_.data.size_);
        ASSERT(ans == ErrorCode::OK);
        Finish(in_isr, ErrorCode::OK, info_);
        OnRxDequeue(in_isr);
      }
      else
      {
        Finish(in_isr, ErrorCode::OK, info_);
      }
      return;
    }

    if (state == BusyState::IDLE)
    {
      if (queue_data_->Size() == 0)
      {
        return;
      }

      RecordEvent(busy_);
      return;
    }

    if (state == BusyState::CLEARING || state == BusyState::PROCESSING ||
        state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_DETACHED)
    {
      RecordEvent(busy_);
    }
    return;
  }
}

ErrorCode ReadPort::ClearQueuedData(bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  while (true)
  {
    const BusyState state = busy_.load(std::memory_order_acquire);
    if (state != BusyState::IDLE && state != BusyState::EVENT)
    {
      return ErrorCode::BUSY;
    }

    BusyState expected = state;
    if (!busy_.compare_exchange_weak(expected, BusyState::CLEARING,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire))
    {
      if (expected != BusyState::IDLE && expected != BusyState::EVENT)
      {
        return ErrorCode::BUSY;
      }
      continue;
    }

    const size_t queued_size = queue_data_->Size();
    if (queued_size > 0)
    {
      const ErrorCode pop_ans = queue_data_->PopBatch(nullptr, queued_size);
      ASSERT(pop_ans == ErrorCode::OK);
      PublishAfterConsumer(busy_, BusyState::CLEARING, queue_data_->Size() > 0);
      OnRxDequeue(in_isr);
    }
    else
    {
      PublishAfterConsumer(busy_, BusyState::CLEARING);
    }
    return ErrorCode::OK;
  }
}

void ReadPort::FailAndClearAll(ErrorCode reason, bool in_isr)
{
  ASSERT(queue_data_ != nullptr);

  auto state = busy_.load(std::memory_order_acquire);
  if (state == BusyState::CLEARING || state == BusyState::CLEARING_EVENT ||
      state == BusyState::PROCESSING || state == BusyState::PROCESSING_EVENT)
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return;
  }

  queue_data_->Reset();

  if (state == BusyState::PENDING)
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

  if (state == BusyState::BLOCK_CLAIMED || state == BusyState::BLOCK_CLAIMED_EVENT ||
      state == BusyState::BLOCK_DETACHED || state == BusyState::BLOCK_DETACHED_EVENT)
  {
    return;
  }

  block_result_ = ErrorCode::OK;
  busy_.store(BusyState::IDLE, std::memory_order_release);
}
