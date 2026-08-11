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

  std::thread claimant(
      [&]()
      {
        for (size_t iteration = 0; iteration < CLAIM_RELEASE_RACE_ITERATIONS; ++iteration)
        {
          iteration_start.arrive_and_wait();
          if ((iteration % 3U) == 1U)
          {
            std::this_thread::yield();
          }
          port.ProcessPendingReads(false);
          iteration_done.arrive_and_wait();
        }
      });

  for (size_t iteration = 0; iteration < CLAIM_RELEASE_RACE_ITERATIONS; ++iteration)
  {
    uint8_t received[2] = {};
    OperationPollingStatus status;
    ReadOperation operation(status);
    ASSERT(port(RawData{received, sizeof(received)}, operation) == ErrorCode::OK);

    const uint8_t first = static_cast<uint8_t>((iteration % 125U) + 1U);
    const uint8_t second = static_cast<uint8_t>(first + 125U);
    ASSERT(port.queue_data_->Push(first) == ErrorCode::OK);

    iteration_start.arrive_and_wait();
    if ((iteration % 3U) == 0U)
    {
      std::this_thread::yield();
    }
    ASSERT(port.queue_data_->Push(second) == ErrorCode::OK);
    port.ProcessPendingReads(false);
    iteration_done.arrive_and_wait();

    ASSERT(status.Load() == OperationPollingStatus::DONE);
    ASSERT(received[0] == first);
    ASSERT(received[1] == second);
    ASSERT(port.Size() == 0U);
  }

  claimant.join();
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

  static const uint8_t NESTED_DATA = 0x42;
  ASSERT(port.queue_data_->Push(NESTED_DATA) == ErrorCode::OK);
  port.ProcessPendingReads(false);

  ASSERT(context.nested_callbacks == 1U);
  ASSERT(context.nested_status == ErrorCode::OK);
  ASSERT(context.nested_dequeue_count == 1U);
  ASSERT(nested_data == NESTED_DATA);
  ASSERT(port.dequeue_count == 2U);
}

}  // namespace

/**
 * @brief Test-item function `RunBaseRwReadQueuePendingTests`.
 * @details Execute queued-read producer handoff and callback-reentry scenarios.
 */
void RunBaseRwReadQueuePendingTests()
{
  test_rw_read_claim_release_handles_concurrent_producer_notification();
  test_rw_queued_callback_can_submit_next_read_before_dequeue_notification();
}
