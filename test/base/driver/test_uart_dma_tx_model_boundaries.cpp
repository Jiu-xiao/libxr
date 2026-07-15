#include <array>

#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

void TestZeroAndDmaBlockSizeBoundaries()
{
  for (auto mode : ALL_MODES)
  {
    FakeUartDmaBackend backend;
    WriteHarness write(mode);
    uint8_t unused = 0U;
    ASSERT(backend.Write(&unused, 0U, write.op) == LibXR::ErrorCode::OK);
    if (mode != TestMode::BLOCK)
    {
      write.ExpectFinal(LibXR::ErrorCode::OK);
    }
    ASSERT(backend.StartCount() == 0U);
  }

  for (size_t size : {DMA_BLOCK_SIZE - 1U, DMA_BLOCK_SIZE})
  {
    FakeUartDmaBackend backend;
    std::array<uint8_t, DMA_BLOCK_SIZE> data{};
    for (size_t i = 0U; i < data.size(); ++i)
    {
      data[i] = static_cast<uint8_t>(0x20U + i);
    }
    PollStatus status = PollStatus::READY;
    LibXR::WriteOperation op(status);
    ASSERT(backend.Write(data.data(), size, op) == LibXR::ErrorCode::OK);
    ASSERT(status == PollStatus::DONE);
    AssertStart(backend, 0U, data.data(), size, 0);
  }
}

void TestOversizedIdleRequestIsConsumed()
{
  FakeUartDmaBackend backend;
  std::array<uint8_t, DMA_BLOCK_SIZE + 1U> oversized{};
  const uint8_t valid[] = {0x41U, 0x42U};
  PollStatus oversized_status = PollStatus::READY;
  PollStatus valid_status = PollStatus::READY;
  LibXR::WriteOperation oversized_op(oversized_status);
  LibXR::WriteOperation valid_op(valid_status);

  ASSERT(backend.Write(oversized.data(), oversized.size(), oversized_op) ==
         LibXR::ErrorCode::FAILED);
  ASSERT(oversized_status == PollStatus::ERROR);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);

  ASSERT(backend.Write(valid, sizeof(valid), valid_op) == LibXR::ErrorCode::OK);
  ASSERT(valid_status == PollStatus::DONE);
  AssertStart(backend, 0U, valid, sizeof(valid), 0);
}

void TestOversizedPendingAndQueuedRequestsDoNotBlockValidData()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x51U};
  const uint8_t second[] = {0x52U};
  std::array<uint8_t, DMA_BLOCK_SIZE + 1U> oversized{};
  const uint8_t fourth[] = {0x54U, 0x55U};
  PollStatus first_status = PollStatus::READY;
  PollStatus second_status = PollStatus::READY;
  PollStatus oversized_status = PollStatus::READY;
  PollStatus fourth_status = PollStatus::READY;
  LibXR::WriteOperation first_op(first_status);
  LibXR::WriteOperation second_op(second_status);
  LibXR::WriteOperation oversized_op(oversized_status);
  LibXR::WriteOperation fourth_op(fourth_status);

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(oversized.data(), oversized.size(), oversized_op) ==
         LibXR::ErrorCode::OK);
  ASSERT(backend.Write(fourth, sizeof(fourth), fourth_op) == LibXR::ErrorCode::OK);
  ASSERT(oversized_status == PollStatus::RUNNING);

  backend.Complete(false);
  ASSERT(second_status == PollStatus::DONE);
  ASSERT(oversized_status == PollStatus::ERROR);
  ASSERT(fourth_status == PollStatus::RUNNING);
  AssertStart(backend, 1U, second, sizeof(second), 1);

  backend.Complete(false);
  ASSERT(fourth_status == PollStatus::DONE);
  AssertStart(backend, 2U, fourth, sizeof(fourth), 0);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestWritePortCapacityFailureDoesNotPublishEvent()
{
  FakeUartDmaBackend backend;
  const uint8_t data[DMA_BLOCK_SIZE] = {};
  LibXR::WriteOperation ops[16]{};
  ASSERT(backend.Write(data, sizeof(data), ops[0]) == LibXR::ErrorCode::OK);

  size_t accepted = 1U;
  LibXR::ErrorCode result = LibXR::ErrorCode::OK;
  while (accepted < (sizeof(ops) / sizeof(ops[0])))
  {
    result = backend.Write(data, sizeof(data), ops[accepted]);
    if (result == LibXR::ErrorCode::FULL)
    {
      break;
    }
    ASSERT(result == LibXR::ErrorCode::OK);
    ++accepted;
  }

  ASSERT(result == LibXR::ErrorCode::FULL);
  ASSERT(accepted > 2U);
  for (size_t completed = 0U; completed < accepted && backend.IsBusy(); ++completed)
  {
    backend.Complete(false);
  }
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == accepted);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

}  // namespace

void RunBoundaryTests()
{
  TestZeroAndDmaBlockSizeBoundaries();
  TestOversizedIdleRequestIsConsumed();
  TestOversizedPendingAndQueuedRequestsDoNotBlockValidData();
  TestWritePortCapacityFailureDoesNotPublishEvent();
}

}  // namespace LibXRTest::UartDmaTx
