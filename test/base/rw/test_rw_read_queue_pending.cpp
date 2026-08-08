/**
 * @file test_rw_read_queue_pending.cpp
 * @brief ReadPort claim-release, timeout arbitration, and callback-reentry scenarios.
 * @details The two legacy queue-completion cases are owned by the runtime RW suite.
 * This file keeps the unique state-machine race and callback regressions.
 */
#include <chrono>
#include <semaphore>
#include <thread>

#include "rw_test_common.hpp"

namespace
{

struct ReadClaimReleaseRace
{
  explicit ReadClaimReleaseRace(LibXR::ReadPort& expected) : expected_port(&expected) {}

  LibXR::ReadPort* expected_port;
  std::binary_semaphore claim_waiting{0};
  std::binary_semaphore producer_waiting{0};
};

constexpr auto HOOK_TIMEOUT = std::chrono::seconds(10);
constexpr uint32_t BLOCK_TIMEOUT_MS = 10000;

enum class SecondReadCompletion : uint8_t
{
  QUEUE_OK,
  FAIL_CLEAR,
};

struct ReadBlockResultGeneration
{
  LibXR::ReadPort* expected_port;
  SecondReadCompletion second_completion;
  bool complete_first_at_timeout = false;
  bool in_second_generation = false;
  bool timeout_completion_seen = false;
  bool after_idle_seen = false;
  LibXR::ErrorCode second_result = LibXR::ErrorCode::PENDING;
};

struct QueuedReadCallbackReentry
{
  TrackingReadPort* port = nullptr;
  LibXR::ReadOperation* nested_op = nullptr;
  uint8_t* nested_data = nullptr;
  LibXR::ErrorCode outer_status = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode nested_status = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode nested_submit = LibXR::ErrorCode::FAILED;
  uint32_t outer_callbacks = 0;
  uint32_t nested_callbacks = 0;
  uint32_t outer_dequeue_count = UINT32_MAX;
  uint32_t nested_dequeue_count = UINT32_MAX;
};

void OnNestedQueuedRead(bool, QueuedReadCallbackReentry* context, LibXR::ErrorCode status)
{
  context->nested_status = status;
  context->nested_dequeue_count = context->port->dequeue_count;
  ++context->nested_callbacks;
}

void OnOuterQueuedRead(bool, QueuedReadCallbackReentry* context, LibXR::ErrorCode status)
{
  context->outer_status = status;
  context->outer_dequeue_count = context->port->dequeue_count;
  ++context->outer_callbacks;
  context->nested_submit = (*context->port)(LibXR::RawData{context->nested_data, 1},
                                            *context->nested_op, false);
}

struct ReadBlockClaimHandoff
{
  explicit ReadBlockClaimHandoff(LibXR::ReadPort& expected) : expected_port(&expected) {}

  LibXR::ReadPort* expected_port;
  std::binary_semaphore timeout_waiting{0};
  std::binary_semaphore claim_acquired{0};
  std::binary_semaphore release_claim{0};
};

bool WaitForReadState(LibXR::ReadPort& port, LibXR::ReadPort::BusyState expected)
{
  const auto deadline = std::chrono::steady_clock::now() + HOOK_TIMEOUT;
  while (port.busy_.load(std::memory_order_acquire) != expected)
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

void RunSecondReadGeneration(ReadBlockResultGeneration& context)
{
  using namespace LibXR;

  uint8_t received = 0xA5;
  if (context.second_completion == SecondReadCompletion::QUEUE_OK)
  {
    static const uint8_t SECOND_DATA = 0x72;
    std::thread completer(
        [&]()
        {
          ASSERT(WaitForReadState(*context.expected_port, ReadPort::BusyState::PENDING));
          ASSERT(context.expected_port->queue_data_->Push(SECOND_DATA) == ErrorCode::OK);
          context.expected_port->ProcessPendingReads(false);
        });

    Semaphore sem;
    ReadOperation operation(sem, BLOCK_TIMEOUT_MS);
    context.second_result =
        (*context.expected_port)(RawData{&received, 1}, operation, false);
    completer.join();
    ASSERT(received == SECOND_DATA);
    ASSERT(sem.Value() == 0);
    return;
  }

  std::thread waiter(
      [&]()
      {
        Semaphore sem;
        ReadOperation operation(sem, BLOCK_TIMEOUT_MS);
        context.second_result =
            (*context.expected_port)(RawData{&received, 1}, operation, false);
        ASSERT(sem.Value() == 0);
      });
  ASSERT(WaitForReadState(*context.expected_port, ReadPort::BusyState::PENDING));
  context.expected_port->FailAndClearAll(ErrorCode::FAILED, false);
  waiter.join();
  ASSERT(received == 0xA5);
}

void ExerciseReadBlockResultGeneration(LibXR::ReadPort& port,
                                       LibXR::ReadPortTestCheckpoint checkpoint,
                                       void* raw)
{
  using namespace LibXR;

  auto& context = *static_cast<ReadBlockResultGeneration*>(raw);
  ASSERT(&port == context.expected_port);
  if (context.in_second_generation)
  {
    return;
  }

  if (checkpoint == ReadPortTestCheckpoint::BLOCK_TIMEOUT_BEFORE_CLAIM &&
      context.complete_first_at_timeout)
  {
    ASSERT(!context.timeout_completion_seen);
    static const uint8_t FIRST_DATA = 0x61;
    ASSERT(port.queue_data_->Push(FIRST_DATA) == ErrorCode::OK);
    port.ProcessPendingReads(false);
    context.timeout_completion_seen = true;
    return;
  }

  if (checkpoint != ReadPortTestCheckpoint::BLOCK_AFTER_IDLE_RELEASE)
  {
    return;
  }

  ASSERT(!context.after_idle_seen);
  context.after_idle_seen = true;
  context.in_second_generation = true;
  RunSecondReadGeneration(context);
  context.in_second_generation = false;
}

void CoordinateClaimRelease(LibXR::ReadPort& port,
                            LibXR::ReadPortTestCheckpoint checkpoint, void* raw)
{
  auto& race = *static_cast<ReadClaimReleaseRace*>(raw);
  ASSERT(&port == race.expected_port);
  switch (checkpoint)
  {
    case LibXR::ReadPortTestCheckpoint::CLAIMED_BEFORE_PENDING_RELEASE:
      race.claim_waiting.release();
      ASSERT(race.producer_waiting.try_acquire_for(HOOK_TIMEOUT));
      break;
    case LibXR::ReadPortTestCheckpoint::PRODUCER_OBSERVED_CLAIMED:
      ASSERT((static_cast<uint32_t>(port.busy_.load(std::memory_order_acquire)) &
              static_cast<uint32_t>(LibXR::ReadPort::BusyState::EVENT)) != 0U);
      race.producer_waiting.release();
      break;
    case LibXR::ReadPortTestCheckpoint::READ_BEFORE_PENDING_PUBLISH:
    case LibXR::ReadPortTestCheckpoint::PROCESS_CLAIM_ACQUIRED:
    case LibXR::ReadPortTestCheckpoint::BLOCK_TIMEOUT_BEFORE_CLAIM:
    case LibXR::ReadPortTestCheckpoint::BLOCK_AFTER_IDLE_RELEASE:
      break;
  }
}

void CoordinateBlockClaimHandoff(LibXR::ReadPort& port,
                                 LibXR::ReadPortTestCheckpoint checkpoint, void* raw)
{
  auto& handoff = *static_cast<ReadBlockClaimHandoff*>(raw);
  ASSERT(&port == handoff.expected_port);

  if (checkpoint == LibXR::ReadPortTestCheckpoint::BLOCK_TIMEOUT_BEFORE_CLAIM)
  {
    handoff.timeout_waiting.release();
    ASSERT(handoff.claim_acquired.try_acquire_for(HOOK_TIMEOUT));
    return;
  }

  if (checkpoint == LibXR::ReadPortTestCheckpoint::PROCESS_CLAIM_ACQUIRED)
  {
    handoff.claim_acquired.release();
    ASSERT(handoff.release_claim.try_acquire_for(HOOK_TIMEOUT));
  }
}

void test_rw_read_block_claim_completion_wins_timeout()
{
  using namespace LibXR;

  TrackingReadPort port(8);
  ReadBlockClaimHandoff handoff(port);
  port.SetTestHook(CoordinateBlockClaimHandoff, &handoff);

  static const uint8_t QUEUED[] = {0x31, 0x32};
  uint8_t received[] = {0xA5, 0x5A};
  Semaphore sem;
  ReadOperation operation(sem, 0);
  ErrorCode result = ErrorCode::PENDING;

  std::thread waiter(
      [&]() { result = port(RawData{received, sizeof(received)}, operation, false); });
  ASSERT(handoff.timeout_waiting.try_acquire_for(HOOK_TIMEOUT));
  ASSERT(port.queue_data_->PushBatch(QUEUED, sizeof(QUEUED)) == ErrorCode::OK);

  std::thread completer([&]() { port.ProcessPendingReads(false); });
  ASSERT(WaitForReadState(port, ReadPort::BusyState::CLAIMED));
  handoff.release_claim.release();

  completer.join();
  waiter.join();
  port.SetTestHook(nullptr, nullptr);

  ASSERT(result == ErrorCode::OK);
  ASSERT(std::memcmp(received, QUEUED, sizeof(received)) == 0);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(port.Size() == 0U);
  ASSERT(port.dequeue_count == 1U);
  ASSERT(sem.Value() == 0);

  static const uint8_t SECOND_DATA = 0x43;
  uint8_t second_received = 0xA5;
  ReadOperation second_operation(sem, BLOCK_TIMEOUT_MS);
  ErrorCode second_result = ErrorCode::PENDING;
  std::thread second_waiter(
      [&]() { second_result = port(RawData{&second_received, 1}, second_operation); });
  ASSERT(WaitForReadState(port, ReadPort::BusyState::PENDING));
  ASSERT(port.queue_data_->Push(SECOND_DATA) == ErrorCode::OK);
  port.ProcessPendingReads(false);
  second_waiter.join();

  ASSERT(second_result == ErrorCode::OK);
  ASSERT(second_received == SECOND_DATA);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(sem.Value() == 0);
}

void test_rw_read_block_claim_insufficient_returns_to_timeout()
{
  using namespace LibXR;

  TrackingReadPort port(8);
  ReadBlockClaimHandoff handoff(port);
  port.SetTestHook(CoordinateBlockClaimHandoff, &handoff);

  uint8_t received = 0xA5;
  Semaphore sem;
  ReadOperation operation(sem, 0);
  ErrorCode result = ErrorCode::PENDING;

  std::thread waiter([&]() { result = port(RawData{&received, 1}, operation); });
  ASSERT(handoff.timeout_waiting.try_acquire_for(HOOK_TIMEOUT));

  std::thread completer([&]() { port.ProcessPendingReads(false); });
  ASSERT(WaitForReadState(port, ReadPort::BusyState::CLAIMED));
  handoff.release_claim.release();

  completer.join();
  waiter.join();
  port.SetTestHook(nullptr, nullptr);

  ASSERT(result == ErrorCode::TIMEOUT);
  ASSERT(received == 0xA5);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(port.Size() == 0U);
  ASSERT(port.dequeue_count == 0U);
  ASSERT(sem.Value() == 0);

  static const uint8_t SECOND_DATA = 0x54;
  ReadOperation second_operation(sem, BLOCK_TIMEOUT_MS);
  ErrorCode second_result = ErrorCode::PENDING;
  std::thread second_waiter(
      [&]() { second_result = port(RawData{&received, 1}, second_operation); });
  ASSERT(WaitForReadState(port, ReadPort::BusyState::PENDING));
  ASSERT(port.queue_data_->Push(SECOND_DATA) == ErrorCode::OK);
  port.ProcessPendingReads(false);
  second_waiter.join();

  ASSERT(second_result == ErrorCode::OK);
  ASSERT(received == SECOND_DATA);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(sem.Value() == 0);
}

void test_rw_read_block_normal_wait_preserves_generation_result()
{
  using namespace LibXR;

  ReadPort port(4);
  uint8_t received = 0xA5;
  ErrorCode first_result = ErrorCode::PENDING;
  ReadBlockResultGeneration context{&port, SecondReadCompletion::QUEUE_OK};

  port.SetTestHook(ExerciseReadBlockResultGeneration, &context);
  std::thread waiter(
      [&]()
      {
        Semaphore sem;
        ReadOperation operation(sem, BLOCK_TIMEOUT_MS);
        first_result = port(RawData{&received, 1}, operation, false);
        ASSERT(sem.Value() == 0);
      });

  ASSERT(WaitForReadState(port, ReadPort::BusyState::PENDING));
  port.FailAndClearAll(ErrorCode::FAILED, false);
  waiter.join();
  port.SetTestHook(nullptr, nullptr);

  ASSERT(first_result == ErrorCode::FAILED);
  ASSERT(context.after_idle_seen);
  ASSERT(context.second_result == ErrorCode::OK);
  ASSERT(received == 0xA5);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void test_rw_read_block_timeout_lost_preserves_generation_result()
{
  using namespace LibXR;

  ReadPort port(4);
  uint8_t received = 0;
  Semaphore sem;
  ReadOperation operation(sem, 0);
  ReadBlockResultGeneration context{&port, SecondReadCompletion::FAIL_CLEAR, true};

  port.SetTestHook(ExerciseReadBlockResultGeneration, &context);
  const ErrorCode first_result = port(RawData{&received, 1}, operation, false);
  port.SetTestHook(nullptr, nullptr);

  ASSERT(first_result == ErrorCode::OK);
  ASSERT(context.timeout_completion_seen);
  ASSERT(context.after_idle_seen);
  ASSERT(context.second_result == ErrorCode::FAILED);
  ASSERT(received == 0x61);
  ASSERT(sem.Value() == 0);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void test_rw_read_claim_release_rechecks_insufficient_completion()
{
  using namespace LibXR;

  ReadPort port(4);
  uint8_t received[2] = {};
  OperationPollingStatus status;
  ReadOperation operation(status);
  ASSERT(port(RawData{received, 2}, operation) == ErrorCode::OK);

  static const uint8_t FIRST = 0x51;
  static const uint8_t SECOND = 0x52;
  ASSERT(port.queue_data_->Push(FIRST) == ErrorCode::OK);

  ReadClaimReleaseRace race(port);
  port.SetTestHook(CoordinateClaimRelease, &race);
  std::thread completer([&]() { port.ProcessPendingReads(false); });

  ASSERT(race.claim_waiting.try_acquire_for(HOOK_TIMEOUT));
  ASSERT(port.queue_data_->Push(SECOND) == ErrorCode::OK);
  port.ProcessPendingReads(false);
  completer.join();
  port.SetTestHook(nullptr, nullptr);

  static const uint8_t EXPECTED[] = {FIRST, SECOND};
  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(std::memcmp(received, EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void test_rw_queued_callback_can_submit_next_read_before_dequeue_notification()
{
  using namespace LibXR;

  TrackingReadPort port(4);
  uint8_t outer_data = 0xA5;
  uint8_t nested_data = 0x5A;
  QueuedReadCallbackReentry context{};

  auto nested_callback = Callback<ErrorCode>::Create(OnNestedQueuedRead, &context);
  ReadOperation nested_op(nested_callback);
  context.port = &port;
  context.nested_op = &nested_op;
  context.nested_data = &nested_data;

  auto outer_callback = Callback<ErrorCode>::Create(OnOuterQueuedRead, &context);
  ReadOperation outer_op(outer_callback);
  ASSERT(port(RawData{&outer_data, 1}, outer_op, false) == ErrorCode::OK);

  static const uint8_t OUTER_DATA = 0x31;
  ASSERT(port.queue_data_->Push(OUTER_DATA) == ErrorCode::OK);
  port.ProcessPendingReads(false);

  ASSERT(context.outer_callbacks == 1U);
  ASSERT(context.outer_status == ErrorCode::OK);
  ASSERT(context.outer_dequeue_count == 0U);
  ASSERT(context.nested_submit == ErrorCode::OK);
  ASSERT(context.nested_callbacks == 0U);
  ASSERT(outer_data == OUTER_DATA);
  ASSERT(port.dequeue_count == 1U);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::PENDING);

  static const uint8_t NESTED_DATA = 0x42;
  ASSERT(port.queue_data_->Push(NESTED_DATA) == ErrorCode::OK);
  port.ProcessPendingReads(false);

  ASSERT(context.nested_callbacks == 1U);
  ASSERT(context.nested_status == ErrorCode::OK);
  ASSERT(context.nested_dequeue_count == 1U);
  ASSERT(nested_data == NESTED_DATA);
  ASSERT(port.dequeue_count == 2U);
  ASSERT(port.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

}  // namespace

/**
 * @brief Test-item function `RunBaseRwReadQueuePendingTests`.
 * @details Execute queued-read claim-release, BLOCK timeout, and callback-reentry
 * scenarios. Group concurrency state-machine regressions separately from clear-queue
 * semantics.
 */
void RunBaseRwReadQueuePendingTests()
{
  test_rw_read_block_normal_wait_preserves_generation_result();
  test_rw_read_block_timeout_lost_preserves_generation_result();
  test_rw_read_block_claim_completion_wins_timeout();
  test_rw_read_block_claim_insufficient_returns_to_timeout();
  test_rw_read_claim_release_rechecks_insufficient_completion();
  test_rw_queued_callback_can_submit_next_read_before_dequeue_notification();
}
