#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

struct LegacyStartBackend
{
  bool StartDmaTx(uint8_t*, size_t, int);
};

template <typename Model>
concept HasHardwareContextCompletion =
    requires(Model& model, LibXR::UartHardwareGate::OwnerContext& context) {
      model.OnTransferDone(false, context);
    };

using ContextBackendModel = LibXR::UartDmaTxModel<FakeUartDmaBackend>;
using LegacyBackendModel = LibXR::UartDmaTxModel<LegacyStartBackend>;

static_assert(HasHardwareContextCompletion<ContextBackendModel>);
static_assert(!HasHardwareContextCompletion<LegacyBackendModel>);
static_assert(requires(LegacyBackendModel& model) { model.OnTransferDone(false); });

void TestIdleAndDuplicateTerminalEvents()
{
  FakeUartDmaBackend backend;
  backend.Complete(false);
  backend.Error(true);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 0U);

  const uint8_t data[] = {0x11U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::DONE);
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  backend.Complete(true);
  backend.Error(true);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
}

void TestActivePendingQueuedPipeline()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {1U, 2U, 3U};
  const uint8_t second[] = {4U, 5U};
  const uint8_t third[] = {6U};
  PollStatus statuses[] = {PollStatus::READY, PollStatus::READY, PollStatus::READY};
  LibXR::WriteOperation first_op(statuses[0]);
  LibXR::WriteOperation second_op(statuses[1]);
  LibXR::WriteOperation third_op(statuses[2]);

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(third, sizeof(third), third_op) == LibXR::ErrorCode::OK);
  ASSERT(statuses[0] == PollStatus::DONE);
  ASSERT(statuses[1] == PollStatus::RUNNING);
  ASSERT(statuses[2] == PollStatus::RUNNING);
  AssertStart(backend, 0U, first, sizeof(first), 0);

  backend.Complete(false);
  ASSERT(statuses[1] == PollStatus::DONE);
  ASSERT(statuses[2] == PollStatus::RUNNING);
  AssertStart(backend, 1U, second, sizeof(second), 1);

  backend.Complete(true);
  ASSERT(statuses[2] == PollStatus::DONE);
  AssertStart(backend, 2U, third, sizeof(third), 0);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestRecoveredErrorMatchesCompletionProgression()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x21U};
  const uint8_t second[] = {0x22U};
  PollStatus first_status = PollStatus::READY;
  PollStatus second_status = PollStatus::READY;
  LibXR::WriteOperation first_op(first_status);
  LibXR::WriteOperation second_op(second_status);

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  backend.Error(true);

  ASSERT(first_status == PollStatus::DONE);
  ASSERT(second_status == PollStatus::DONE);
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, second, sizeof(second), 1);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestPromotedStartFailureAbortsRetainedPipeline()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x31U};
  const uint8_t second[] = {0x32U};
  const uint8_t third[] = {0x33U};
  PollStatus first_status = PollStatus::READY;
  PollStatus second_status = PollStatus::READY;
  PollStatus third_status = PollStatus::READY;
  LibXR::WriteOperation first_op(first_status);
  LibXR::WriteOperation second_op(second_status);
  LibXR::WriteOperation third_op(third_status);

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(third, sizeof(third), third_op) == LibXR::ErrorCode::OK);
  backend.SetStartAllowed(false);
  backend.Complete(true);

  ASSERT(first_status == PollStatus::DONE);
  ASSERT(second_status == PollStatus::ERROR);
  ASSERT(third_status == PollStatus::ERROR);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestQueuedStartRetryStagesPayloadBeforeRetry()
{
  FakeUartDmaBackend backend;
  const uint8_t data[] = {0x41U, 0x42U, 0x43U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  backend.RetryNextStart();
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);

  ASSERT(status == PollStatus::RUNNING);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.ResumeStart(true);

  ASSERT(status == PollStatus::DONE);
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
  AssertStart(backend, 0U, data, sizeof(data), 0);

  backend.ResumeStart(false);
  ASSERT(backend.StartCount() == 1U);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestPendingStartRetryPreservesPreloadedBuffer()
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x51U};
  const uint8_t pending[] = {0x52U, 0x53U};
  PollStatus active_status = PollStatus::READY;
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation active_op(active_status);
  LibXR::WriteOperation pending_op(pending_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);
  ASSERT(active_status == PollStatus::DONE);
  ASSERT(pending_status == PollStatus::RUNNING);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.RetryNextStart();
  backend.Complete(true);

  ASSERT(pending_status == PollStatus::RUNNING);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.ResumeStart(false);

  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
  AssertStart(backend, 1U, pending, sizeof(pending), 1);

  backend.ResumeStart(true);
  ASSERT(backend.StartCount() == 2U);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestRetriedPendingIsFailedByConfig()
{
  FakeUartDmaBackend backend;
  const uint8_t data[] = {0x5AU};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  backend.RetryNextStart();
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::RUNNING);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.Configure(0x5AU);

  ASSERT(status == PollStatus::ERROR);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestFailedStartDoesNotAdvanceActiveBlock()
{
  FakeUartDmaBackend backend;
  const uint8_t failed_data[] = {0x5BU};
  const uint8_t next_data[] = {0x5CU};
  PollStatus failed_status = PollStatus::READY;
  PollStatus next_status = PollStatus::READY;
  LibXR::WriteOperation failed_op(failed_status);
  LibXR::WriteOperation next_op(next_status);

  backend.SetStartAllowed(false);
  ASSERT(backend.Write(failed_data, sizeof(failed_data), failed_op) ==
         LibXR::ErrorCode::FAILED);
  ASSERT(failed_status == PollStatus::ERROR);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.StartCount() == 0U);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.SetStartAllowed(true);
  ASSERT(backend.Write(next_data, sizeof(next_data), next_op) == LibXR::ErrorCode::OK);
  ASSERT(next_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 1U);
  AssertStart(backend, 0U, next_data, sizeof(next_data), 0);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestRetriedHeadStartsBeforeLaterWrite()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x5DU};
  const uint8_t second[] = {0x5EU};
  PollStatus first_status = PollStatus::READY;
  PollStatus second_status = PollStatus::READY;
  LibXR::WriteOperation first_op(first_status);
  LibXR::WriteOperation second_op(second_status);

  backend.RetryNextStart();
  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(first_status == PollStatus::RUNNING);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);

  ASSERT(first_status == PollStatus::DONE);
  ASSERT(second_status == PollStatus::RUNNING);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);
  AssertStart(backend, 0U, first, sizeof(first), 0);

  backend.Complete(false);
  ASSERT(second_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, second, sizeof(second), 1);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

}  // namespace

void RunStateTests()
{
  TestIdleAndDuplicateTerminalEvents();
  TestActivePendingQueuedPipeline();
  TestRecoveredErrorMatchesCompletionProgression();
  TestPromotedStartFailureAbortsRetainedPipeline();
  TestQueuedStartRetryStagesPayloadBeforeRetry();
  TestPendingStartRetryPreservesPreloadedBuffer();
  TestRetriedPendingIsFailedByConfig();
  TestFailedStartDoesNotAdvanceActiveBlock();
  TestRetriedHeadStartsBeforeLaterWrite();
}

}  // namespace LibXRTest::UartDmaTx
