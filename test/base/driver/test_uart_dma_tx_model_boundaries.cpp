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

void TestOversizedRequestIsRejectedByPortCapacity()
{
  FakeUartDmaBackend backend;
  std::array<uint8_t, DMA_BLOCK_SIZE + 1U> oversized{};
  PollStatus oversized_status = PollStatus::READY;
  LibXR::WriteOperation oversized_op(oversized_status);

  ASSERT(backend.Write(oversized.data(), oversized.size(), oversized_op) ==
         LibXR::ErrorCode::FULL);
  ASSERT(oversized_status == PollStatus::READY);
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

void TestOperationIsArmedBeforeBackendSeesMetadata()
{
  struct Probe
  {
    static LibXR::ErrorCode WriteFun(LibXR::WritePort& port, bool in_isr)
    {
      LibXR::WriteInfoBlock info{};
      ASSERT(port.queue_info_->Pop(info) == LibXR::ErrorCode::OK);
      ASSERT(info.op.type == LibXR::WriteOperation::OperationType::POLLING);
      ASSERT(*info.op.data.status == PollStatus::RUNNING);
      ASSERT(port.queue_data_->PopBatch(nullptr, info.data.size_) ==
             LibXR::ErrorCode::OK);
      port.Finish(in_isr, LibXR::ErrorCode::OK, info);
      return LibXR::ErrorCode::OK;
    }
  };

  LibXR::WritePort port(1U, DMA_BLOCK_SIZE);
  port = Probe::WriteFun;
  const uint8_t data[] = {0x61U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);
  ASSERT(port(LibXR::ConstRawData(data, sizeof(data)), op, false) ==
         LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::DONE);

  PollStatus stream_status = PollStatus::READY;
  LibXR::WriteOperation stream_op(stream_status);
  LibXR::WritePort::Stream stream(&port, stream_op);
  ASSERT(stream.Write(LibXR::ConstRawData(data, sizeof(data))) == LibXR::ErrorCode::OK);
  ASSERT(stream.Commit() == LibXR::ErrorCode::OK);
  ASSERT(stream_status == PollStatus::DONE);
}

}  // namespace

void RunBoundaryTests()
{
  TestZeroAndDmaBlockSizeBoundaries();
  TestOversizedRequestIsRejectedByPortCapacity();
  TestWritePortCapacityFailureDoesNotPublishEvent();
  TestOperationIsArmedBeforeBackendSeesMetadata();
}

}  // namespace LibXRTest::UartDmaTx
