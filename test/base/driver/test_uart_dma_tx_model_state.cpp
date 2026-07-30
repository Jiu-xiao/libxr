#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

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

}  // namespace

void RunStateTests()
{
  TestIdleAndDuplicateTerminalEvents();
  TestActivePendingQueuedPipeline();
  TestRecoveredErrorMatchesCompletionProgression();
  TestPromotedStartFailureAbortsRetainedPipeline();
}

}  // namespace LibXRTest::UartDmaTx
