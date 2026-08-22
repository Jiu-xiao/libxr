/**
 * @file test_rw_read_queue_pending.cpp
 * @brief ReadPort producer handoff and callback-reentry scenarios.
 * @details The two legacy queue-completion cases are owned by the runtime RW suite.
 * This file keeps black-box concurrency and callback regressions.
 */
#include <barrier>
#include <thread>

#include "rw_test_common.hpp"

namespace
{

constexpr size_t CLAIM_RELEASE_RACE_ITERATIONS = 1024;

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

void test_rw_read_claim_release_handles_concurrent_producer_notification()
{
  using namespace LibXR;

  ReadPort port(4);
  std::barrier<> iteration_start(2);
  std::barrier<> iteration_done(2);

  std::thread producer(
      [&]()
      {
        for (size_t iteration = 0; iteration < CLAIM_RELEASE_RACE_ITERATIONS; ++iteration)
        {
          iteration_start.arrive_and_wait();
          const uint8_t first = static_cast<uint8_t>((iteration % 125U) + 1U);
          const uint8_t second = static_cast<uint8_t>(first + 125U);
          {
            auto queue = port.GetReadQueue();
            ASSERT(queue.Push(first) == ErrorCode::OK);
            if ((iteration % 3U) == 1U)
            {
              std::this_thread::yield();
            }
            ASSERT(queue.Push(second) == ErrorCode::OK);
            queue.Publish();
          }
          iteration_done.arrive_and_wait();
        }
      });

  for (size_t iteration = 0; iteration < CLAIM_RELEASE_RACE_ITERATIONS; ++iteration)
  {
    uint8_t received[2] = {};
    OperationPollingStatus status;
    ReadOperation operation(status);
    const uint8_t first = static_cast<uint8_t>((iteration % 125U) + 1U);
    const uint8_t second = static_cast<uint8_t>(first + 125U);

    iteration_start.arrive_and_wait();
    if ((iteration % 3U) == 0U)
    {
      std::this_thread::yield();
    }
    ASSERT(port(RawData{received, sizeof(received)}, operation) == ErrorCode::OK);
    iteration_done.arrive_and_wait();

    ASSERT(status.Load() == OperationPollingStatus::DONE);
    ASSERT(received[0] == first);
    ASSERT(received[1] == second);
    ASSERT(port.Size() == 0U);
  }

  producer.join();
}

void test_rw_invalid_read_does_not_claim_or_consume_queue()
{
  using namespace LibXR;

  ReadPort port(2);
  static constexpr uint8_t QUEUED = 0x6BU;
  {
    auto queue = port.GetReadQueue();
    ASSERT(queue.Push(QUEUED) == ErrorCode::OK);
    queue.Publish();
  }

  OperationPollingStatus null_status;
  ReadOperation null_operation(null_status);
  ASSERT(port(RawData{nullptr, 1U}, null_operation) == ErrorCode::PTR_NULL);
  ASSERT(null_status.Load() == OperationPollingStatus::READY);
  ASSERT(port.Size() == 1U);

  uint8_t oversized[3] = {};
  OperationPollingStatus oversized_status;
  ReadOperation oversized_operation(oversized_status);
  ASSERT(port(RawData{oversized, sizeof(oversized)}, oversized_operation) ==
         ErrorCode::SIZE_ERR);
  ASSERT(oversized_status.Load() == OperationPollingStatus::READY);
  ASSERT(port.Size() == 1U);

  uint8_t received = 0U;
  OperationPollingStatus valid_status;
  ReadOperation valid_operation(valid_status);
  ASSERT(port(RawData{&received, 1U}, valid_operation) == ErrorCode::OK);
  ASSERT(valid_status.Load() == OperationPollingStatus::DONE);
  ASSERT(received == QUEUED);
  ASSERT(port.Size() == 0U);
}

void test_rw_queued_callback_can_submit_next_read_after_dequeue_notification()
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
  {
    auto queue = port.GetReadQueue();
    ASSERT(queue.Push(OUTER_DATA) == ErrorCode::OK);
    queue.Publish();
  }

  ASSERT(context.outer_callbacks == 1U);
  ASSERT(context.outer_status == ErrorCode::OK);
  ASSERT(context.outer_dequeue_count == 1U);
  ASSERT(context.nested_submit == ErrorCode::OK);
  ASSERT(context.nested_callbacks == 0U);
  ASSERT(outer_data == OUTER_DATA);
  ASSERT(port.dequeue_count == 1U);

  static const uint8_t NESTED_DATA = 0x42;
  {
    auto queue = port.GetReadQueue();
    ASSERT(queue.Push(NESTED_DATA) == ErrorCode::OK);
    queue.Publish();
  }

  ASSERT(context.nested_callbacks == 1U);
  ASSERT(context.nested_status == ErrorCode::OK);
  ASSERT(context.nested_dequeue_count == 2U);
  ASSERT(nested_data == NESTED_DATA);
  ASSERT(port.dequeue_count == 2U);
}

void test_rw_busy_read_does_not_modify_rejected_operation_or_pending_request()
{
  using namespace LibXR;

  ReadPort port(2);
  uint8_t first_data = 0;
  OperationPollingStatus first_status;
  ReadOperation first_operation(first_status);
  ASSERT(port(RawData{&first_data, 1}, first_operation) == ErrorCode::OK);
  ASSERT(first_status.Load() == OperationPollingStatus::RUNNING);

  uint8_t rejected_data = 0xA5;
  OperationPollingStatus rejected_status;
  ReadOperation rejected_operation(rejected_status);
  ASSERT(port(RawData{&rejected_data, 1}, rejected_operation) == ErrorCode::BUSY);
  ASSERT(rejected_status.Load() == OperationPollingStatus::READY);

  static const uint8_t RECEIVED = 0x5A;
  {
    auto queue = port.GetReadQueue();
    ASSERT(queue.Push(RECEIVED) == ErrorCode::OK);
    queue.Publish();
  }

  ASSERT(first_status.Load() == OperationPollingStatus::DONE);
  ASSERT(first_data == RECEIVED);
  ASSERT(rejected_status.Load() == OperationPollingStatus::READY);
  ASSERT(rejected_data == 0xA5);
}

}  // namespace

/**
 * @brief Test-item function `RunBaseRwReadQueuePendingTests`.
 * @details Execute queued-read producer handoff and callback-reentry scenarios.
 */
void RunBaseRwReadQueuePendingTests()
{
  test_rw_read_claim_release_handles_concurrent_producer_notification();
  test_rw_invalid_read_does_not_claim_or_consume_queue();
  test_rw_queued_callback_can_submit_next_read_after_dequeue_notification();
  test_rw_busy_read_does_not_modify_rejected_operation_or_pending_request();
}
