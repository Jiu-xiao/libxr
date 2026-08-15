/**
 * @file test_rw_publication.cpp
 * @brief WritePort producer-publication and backend-completion ordering scenarios.
 */
#include "rw_test_common.hpp"

namespace
{

class PublicationPort : public LibXR::WritePort
{
 public:
  PublicationPort() : WritePort(4U, 16U) { WritePort::operator=(WriteFun); }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& base, bool in_isr)
  {
    auto& port = static_cast<PublicationPort&>(base);
    if (port.terminal_valid_)
    {
      ++port.retry_count_;
      ASSERT(port.PublishTerminal(in_isr));
      return LibXR::ErrorCode::PENDING;
    }

    LibXR::WriteInfoBlock info{};
    {
      auto dequeue = port.BeginDequeue(in_isr);
      ASSERT(dequeue.PopInfo(info) == LibXR::ErrorCode::OK);
      ASSERT(dequeue.DiscardData(info.data.size_) == LibXR::ErrorCode::OK);
      port.terminal_ = info;
      port.terminal_valid_ = true;
    }

    if (port.simulate_async_terminal_during_submit_)
    {
      ASSERT(!port.TryPublishBackendCompletion());
      ASSERT(!port.TryPublishBackendCompletion());
      port.blocked_count_ += 2U;
    }
    return LibXR::ErrorCode::PENDING;
  }

  bool PublishTerminal(bool in_isr)
  {
    ASSERT(terminal_valid_);
    if (!TryPublishBackendCompletion())
    {
      return false;
    }

    LibXR::WriteInfoBlock info = terminal_;
    terminal_ = {};
    terminal_valid_ = false;
    Finish(in_isr, LibXR::ErrorCode::OK, info);
    return true;
  }

  bool simulate_async_terminal_during_submit_ = true;
  bool terminal_valid_ = false;
  size_t blocked_count_ = 0U;
  size_t retry_count_ = 0U;
  LibXR::WriteInfoBlock terminal_{};
};

struct ReentryContext
{
  PublicationPort* port = nullptr;
  LibXR::ErrorCode nested_result = LibXR::ErrorCode::FAILED;
};

void CompleteAndReenter(bool, ReentryContext* context, LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  static const uint8_t NESTED[] = {0x61U};
  LibXR::WriteOperation nested_operation;
  context->nested_result =
      (*context->port)(LibXR::ConstRawData{NESTED, sizeof(NESTED)}, nested_operation);
}

void test_async_completion_waits_for_publication_release()
{
  using namespace LibXR;

  PublicationPort port;
  ReentryContext context{&port};
  auto callback = Callback<ErrorCode>::Create(CompleteAndReenter, &context);
  WriteOperation operation(callback);
  static const uint8_t OUTER[] = {0x51U, 0x52U};

  ASSERT(port(ConstRawData{OUTER, sizeof(OUTER)}, operation) == ErrorCode::OK);
  ASSERT(context.nested_result == ErrorCode::OK);
  ASSERT(port.blocked_count_ == 4U);
  ASSERT(port.retry_count_ == 2U);
  ASSERT(!port.terminal_valid_);
}

void test_async_completion_after_publication_release()
{
  using namespace LibXR;

  PublicationPort port;
  port.simulate_async_terminal_during_submit_ = false;
  OperationPollingStatus status;
  WriteOperation operation(status);
  static const uint8_t PAYLOAD[] = {0x21U, 0x22U};

  ASSERT(port(ConstRawData{PAYLOAD, sizeof(PAYLOAD)}, operation) == ErrorCode::OK);
  ASSERT(status == OperationPollingStatus::RUNNING);
  ASSERT(port.PublishTerminal(false));
  ASSERT(status == OperationPollingStatus::DONE);
  ASSERT(port.retry_count_ == 0U);
}

void test_old_completion_is_allowed_during_owner_copy()
{
  using namespace LibXR;

  PublicationPort port;
  port.simulate_async_terminal_during_submit_ = false;
  ReentryContext context{&port};
  auto callback = Callback<ErrorCode>::Create(CompleteAndReenter, &context);
  WriteOperation first_operation(callback);
  static const uint8_t FIRST[] = {0x31U};
  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(port.terminal_valid_);

  WriteOperation stream_operation;
  WritePort::Stream stream(&port, stream_operation);
  static const uint8_t SECOND[] = {0x41U, 0x42U};
  ASSERT(stream.Write(ConstRawData{SECOND, sizeof(SECOND)}) == ErrorCode::OK);

  // OWNER means that the current producer (including a deferred promoter copying its
  // caller buffer) has not published new metadata. The fixed terminal is therefore the
  // older record and must not be delayed by the PUBLISHING gate.
  ASSERT(port.PublishTerminal(false));
  ASSERT(context.nested_result == ErrorCode::BUSY);

  port.simulate_async_terminal_during_submit_ = true;
  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(port.blocked_count_ == 2U);
  ASSERT(port.retry_count_ == 1U);
  ASSERT(!port.terminal_valid_);
}

void test_block_completion_retry_precedes_wait()
{
  using namespace LibXR;

  PublicationPort port;
  Semaphore semaphore;
  WriteOperation operation(semaphore, 100U);
  static const uint8_t PAYLOAD[] = {0x71U, 0x72U};

  ASSERT(port(ConstRawData{PAYLOAD, sizeof(PAYLOAD)}, operation) == ErrorCode::OK);
  ASSERT(port.blocked_count_ == 2U);
  ASSERT(port.retry_count_ == 1U);
  ASSERT(semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);
}

}  // namespace

void RunBaseRwPublicationTests()
{
  test_async_completion_waits_for_publication_release();
  test_async_completion_after_publication_release();
  test_old_completion_is_allowed_during_owner_copy();
  test_block_completion_retry_precedes_wait();
}
