#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

void TestSynchronousStartAcrossOperationModes()
{
  const uint8_t data[] = {0x61U, 0x62U};
  for (auto mode : ALL_MODES)
  {
    FakeUartDmaBackend backend;
    WriteHarness write(mode);
    ASSERT(backend.Write(data, sizeof(data), write.op) == LibXR::ErrorCode::OK);
    write.ExpectFinal(LibXR::ErrorCode::OK);
    AssertStart(backend, 0U, data, sizeof(data), 0);
  }
}

void TestSynchronousStartFailureAcrossOperationModes()
{
  const uint8_t data[] = {0x63U};
  for (auto mode : ALL_MODES)
  {
    FakeUartDmaBackend backend;
    backend.SetStartAllowed(false);
    WriteHarness write(mode);
    ASSERT(backend.Write(data, sizeof(data), write.op) == LibXR::ErrorCode::FAILED);
    write.ExpectFinal(LibXR::ErrorCode::FAILED);
    ASSERT(!backend.IsBusy());
    ASSERT(backend.QueuedInfoCount() == 0U);
    ASSERT(backend.QueuedDataSize() == 0U);
  }
}

void TestPendingCompletionAcrossAsyncOperationModes()
{
  const uint8_t active[] = {0x64U};
  const uint8_t pending[] = {0x65U, 0x66U};
  for (auto mode : ASYNC_MODES)
  {
    FakeUartDmaBackend backend;
    LibXR::WriteOperation active_op;
    WriteHarness write(mode);
    ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
    ASSERT(backend.Write(pending, sizeof(pending), write.op) == LibXR::ErrorCode::OK);
    write.ExpectPendingSubmitted();

    backend.Complete(true);
    write.ExpectFinal(LibXR::ErrorCode::OK);
    AssertStart(backend, 1U, pending, sizeof(pending), 1);
  }
}

void TestPendingBlockCompletion(bool start_allowed, LibXR::ErrorCode expected)
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x67U};
  const uint8_t pending[] = {0x68U};
  LibXR::WriteOperation active_op;
  WriteHarness write(TestMode::BLOCK, WAIT_TIMEOUT_MS);
  std::atomic<uint32_t> done{0U};
  std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  std::thread waiter(
      [&]
      {
        result.store(
            static_cast<uint32_t>(backend.Write(pending, sizeof(pending), write.op)),
            std::memory_order_release);
        done.store(1U, std::memory_order_release);
      });

  ASSERT(WaitUntil([&] { return backend.QueuedInfoCount() == 1U; }));
  backend.SetStartAllowed(start_allowed);
  backend.Complete(true);
  ASSERT(WaitUntil([&] { return done.load(std::memory_order_acquire) != 0U; }));
  waiter.join();
  ASSERT(static_cast<LibXR::ErrorCode>(result.load(std::memory_order_acquire)) ==
         expected);
}

void TestBlockTimeoutDoesNotCancelTransfer()
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x69U};
  const uint8_t pending[] = {0x6AU};
  const uint8_t next[] = {0x6BU};
  LibXR::WriteOperation active_op;
  WriteHarness timed_write(TestMode::BLOCK, 10U);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), timed_write.op) ==
         LibXR::ErrorCode::TIMEOUT);

  backend.Complete(false);
  ASSERT(backend.IsBusy());
  AssertStart(backend, 1U, pending, sizeof(pending), 1);

  LibXR::WriteOperation next_op;
  ASSERT(backend.Write(next, sizeof(next), next_op) == LibXR::ErrorCode::OK);
  backend.Complete(false);
  AssertStart(backend, 2U, next, sizeof(next), 0);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

}  // namespace

void RunOperationTests()
{
  TestSynchronousStartAcrossOperationModes();
  TestSynchronousStartFailureAcrossOperationModes();
  TestPendingCompletionAcrossAsyncOperationModes();
  TestPendingBlockCompletion(true, LibXR::ErrorCode::OK);
  TestPendingBlockCompletion(false, LibXR::ErrorCode::FAILED);
  TestBlockTimeoutDoesNotCancelTransfer();
}

}  // namespace LibXRTest::UartDmaTx
