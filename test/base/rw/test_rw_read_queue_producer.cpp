/**
 * @file test_rw_read_queue_producer.cpp
 * @brief ReadPort producer-scope publication scenarios.
 */
#include <array>
#include <cstring>

#include "read_port.hpp"

namespace
{

class ProducerTrackingReadPort : public LibXR::ReadPort
{
 public:
  using LibXR::ReadPort::ReadPort;

  void OnRxDequeue(bool) override { ++dequeue_count; }

  uint32_t dequeue_count = 0U;
};

void test_empty_scope_is_a_noop()
{
  using namespace LibXR;

  ReadPort port(4U);
  {
    auto queue = port.GetReadQueue();
    ASSERT(queue.EmptySize() == 4U);
    ASSERT(queue.Capacity() == 4U);
    ASSERT(queue.PushBatch(nullptr, 0U) == ErrorCode::OK);
    bool writer_called = false;
    ASSERT(queue.PushWithWriter(0U,
                                [&writer_called](uint8_t*, size_t)
                                {
                                  writer_called = true;
                                  return ErrorCode::OK;
                                }) == ErrorCode::OK);
    ASSERT(!writer_called);
    queue.Publish();
  }
  ASSERT(port.Size() == 0U);
  ASSERT(port.Capacity() == 4U);

  // A clean scope may also end without Publish(); its destructor must remain passive.
  {
    auto queue = port.GetReadQueue();
  }
}

void test_single_push_completes_only_at_publish()
{
  using namespace LibXR;

  ProducerTrackingReadPort port(4U);
  uint8_t received = 0U;
  OperationPollingStatus status;
  ReadOperation operation(status);
  ASSERT(port(RawData{&received, 1U}, operation) == ErrorCode::OK);
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);

  auto queue = port.GetReadQueue();
  ASSERT(queue.Push(0x51U) == ErrorCode::OK);
  ASSERT(port.Size() == 1U);
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);
  ASSERT(port.dequeue_count == 0U);

  queue.Publish();
  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(received == 0x51U);
  ASSERT(port.Size() == 0U);
  ASSERT(port.dequeue_count == 1U);

#ifndef LIBXR_DEV_ASSERT_BUILD
  ASSERT(queue.Push(0x52U) == ErrorCode::STATE_ERR);
  queue.Publish();
  ASSERT(port.Size() == 0U);
  ASSERT(port.dequeue_count == 1U);
#endif
}

void test_multiple_push_forms_share_one_publish_boundary()
{
  using namespace LibXR;

  ProducerTrackingReadPort port(8U);
  std::array<uint8_t, 6U> received{};
  static constexpr std::array<uint8_t, 6U> EXPECTED = {0x11U, 0x21U, 0x22U,
                                                       0x31U, 0x32U, 0x33U};
  OperationPollingStatus status;
  ReadOperation operation(status);
  ASSERT(port(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);

  auto queue = port.GetReadQueue();
  ASSERT(queue.Push(EXPECTED[0]) == ErrorCode::OK);
  ASSERT(queue.PushBatch(EXPECTED.data() + 1U, 2U) == ErrorCode::OK);

  size_t writer_offset = 3U;
  ASSERT(queue.PushWithWriter(3U,
                              [&writer_offset](uint8_t* buffer, size_t size)
                              {
                                std::memcpy(buffer, EXPECTED.data() + writer_offset,
                                            size);
                                writer_offset += size;
                                return ErrorCode::OK;
                              }) == ErrorCode::OK);
  ASSERT(writer_offset == EXPECTED.size());
  ASSERT(port.Size() == EXPECTED.size());
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);

  queue.Publish();
  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(received == EXPECTED);
  ASSERT(port.dequeue_count == 1U);
}

void test_failed_pushes_do_not_commit_or_require_publication()
{
  using namespace LibXR;

  ReadPort port(2U);
  uint8_t received[2] = {};
  OperationPollingStatus status;
  ReadOperation operation(status);
  ASSERT(port(RawData{received, sizeof(received)}, operation) == ErrorCode::OK);

  {
    auto queue = port.GetReadQueue();
    static const uint8_t TOO_LARGE[] = {0x01U, 0x02U, 0x03U};
    ASSERT(queue.PushBatch(TOO_LARGE, sizeof(TOO_LARGE)) == ErrorCode::FULL);
    ASSERT(queue.PushWithWriter(1U, [](uint8_t*, size_t)
                                { return ErrorCode::INIT_ERR; }) == ErrorCode::INIT_ERR);
    ASSERT(port.Size() == 0U);
    // No successful positive-length push means destruction does not require Publish().
  }
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);

  static const uint8_t EXPECTED[] = {0x41U, 0x42U};
  auto queue = port.GetReadQueue();
  ASSERT(queue.PushBatch(EXPECTED, sizeof(EXPECTED)) == ErrorCode::OK);
  ASSERT(queue.Push(0x43U) == ErrorCode::FULL);
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);
  queue.Publish();

  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(std::memcmp(received, EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(port.Size() == 0U);
}

struct ReentryContext
{
  ProducerTrackingReadPort* port = nullptr;
  LibXR::ReadOperation* nested_operation = nullptr;
  uint8_t* nested_received = nullptr;
  LibXR::ErrorCode nested_submit = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode nested_push = LibXR::ErrorCode::FAILED;
  LibXR::OperationPollingStatus* nested_status = nullptr;
  uint32_t callback_count = 0U;
};

void CompleteAndPublishNested(bool in_isr, ReentryContext* context,
                              LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ASSERT(context->port->dequeue_count == 1U);
  ++context->callback_count;
  context->nested_submit = (*context->port)(LibXR::RawData{context->nested_received, 1U},
                                            *context->nested_operation, in_isr);

  auto queue = context->port->GetReadQueue(in_isr);
  context->nested_push = queue.Push(0x72U);
  queue.Publish();
  ASSERT(context->nested_status->Load() == LibXR::OperationPollingStatus::DONE);
}

void test_publish_is_terminal_before_callback_reentry()
{
  using namespace LibXR;

  ProducerTrackingReadPort port(4U);
  uint8_t nested_received = 0U;
  OperationPollingStatus nested_status;
  ReadOperation nested_operation(nested_status);
  ReentryContext context{&port,
                         &nested_operation,
                         &nested_received,
                         ErrorCode::FAILED,
                         ErrorCode::FAILED,
                         &nested_status};

  auto callback = Callback<ErrorCode>::Create(CompleteAndPublishNested, &context);
  ReadOperation outer_operation(callback);
  uint8_t outer_received = 0U;
  ASSERT(port(RawData{&outer_received, 1U}, outer_operation) == ErrorCode::OK);

  auto queue = port.GetReadQueue();
  ASSERT(queue.Push(0x61U) == ErrorCode::OK);
  queue.Publish();

  ASSERT(context.callback_count == 1U);
  ASSERT(context.nested_submit == ErrorCode::OK);
  ASSERT(context.nested_push == ErrorCode::OK);
  ASSERT(outer_received == 0x61U);
  ASSERT(nested_received == 0x72U);
  ASSERT(nested_status.Load() == OperationPollingStatus::DONE);
  ASSERT(port.dequeue_count == 2U);
  ASSERT(port.Size() == 0U);
}

}  // namespace

void RunBaseRwReadQueueProducerTests()
{
  test_empty_scope_is_a_noop();
  test_single_push_completes_only_at_publish();
  test_multiple_push_forms_share_one_publish_boundary();
  test_failed_pushes_do_not_commit_or_require_publication();
  test_publish_is_terminal_before_callback_reentry();
}
