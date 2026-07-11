#include <array>

#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

void TestConcurrentWriteAndTerminalEventsMakeProgress()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x71U};
  const uint8_t second[] = {0x72U};
  const uint8_t third[] = {0x73U};
  PollStatus statuses[] = {PollStatus::READY, PollStatus::READY, PollStatus::READY};
  LibXR::WriteOperation ops[] = {LibXR::WriteOperation(statuses[0]),
                                 LibXR::WriteOperation(statuses[1]),
                                 LibXR::WriteOperation(statuses[2])};
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), ops[0]) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), ops[1]) == LibXR::ErrorCode::OK);
  backend.BlockNextStart();
  std::thread owner(
      [&]
      {
        backend.Complete(false);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  ASSERT(backend.Write(third, sizeof(third), ops[2], true) == LibXR::ErrorCode::OK);
  backend.Error(true);
  backend.Complete(true);
  backend.ReleaseBlockedStart();
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(statuses[0] == PollStatus::DONE);
  ASSERT(statuses[1] == PollStatus::DONE);
  ASSERT(statuses[2] == PollStatus::DONE);
  ASSERT(backend.StartCount() == 3U);
  AssertStart(backend, 2U, third, sizeof(third), 0);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestAbortAbsorbsCoalescedTerminalEvents()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x74U};
  const uint8_t second[] = {0x75U};
  const uint8_t third[] = {0x76U};
  PollStatus statuses[] = {PollStatus::READY, PollStatus::READY, PollStatus::READY};
  LibXR::WriteOperation ops[] = {LibXR::WriteOperation(statuses[0]),
                                 LibXR::WriteOperation(statuses[1]),
                                 LibXR::WriteOperation(statuses[2])};
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), ops[0]) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), ops[1]) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(third, sizeof(third), ops[2]) == LibXR::ErrorCode::OK);
  backend.SetStartAllowed(false);
  backend.BlockNextStart();
  std::thread owner(
      [&]
      {
        backend.Complete(false);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  backend.Error(true);
  backend.Complete(true);
  backend.ReleaseBlockedStart();
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(statuses[0] == PollStatus::DONE);
  ASSERT(statuses[1] == PollStatus::ERROR);
  ASSERT(statuses[2] == PollStatus::ERROR);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestStreamPublicationSurvivesAbortBoundary()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x77U};
  const uint8_t second[] = {0x78U};
  const uint8_t stream_data[] = {0x79U, 0x7AU};
  LibXR::WriteOperation first_op;
  LibXR::WriteOperation second_op;
  PollStatus stream_status = PollStatus::READY;
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  backend.SetStartAllowed(false);
  backend.BlockNextStart();
  std::thread owner(
      [&]
      {
        backend.Complete(false);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  LibXR::WritePort::Stream stream(&backend.Port(), LibXR::WriteOperation(stream_status));
  ASSERT(stream.Write(LibXR::ConstRawData(stream_data, sizeof(stream_data))) ==
         LibXR::ErrorCode::OK);
  backend.ReleaseBlockedStart();
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == sizeof(stream_data));
  backend.SetStartAllowed(true);
  ASSERT(stream.Commit() == LibXR::ErrorCode::OK);
  ASSERT(stream_status == PollStatus::DONE);
  AssertStart(backend, 1U, stream_data, sizeof(stream_data), 1);
}

void TestOwnerContextOverridesTerminalEventSource()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x7BU};
  const uint8_t second[] = {0x7CU};
  const uint8_t third[] = {0x7DU};
  LibXR::WriteOperation first_op;
  LibXR::WriteOperation second_op;
  CallbackProbe third_probe;
  LibXR::WriteOperation third_op(third_probe.callback);
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(third, sizeof(third), third_op) == LibXR::ErrorCode::OK);
  backend.BlockNextStart();
  std::thread thread_owner(
      [&]
      {
        backend.Complete(false);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  backend.Complete(true);
  backend.ReleaseBlockedStart();
  ASSERT(third_probe.WaitForCount(1U));
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  thread_owner.join();
  ASSERT(third_probe.in_isr.load(std::memory_order_acquire) == 0U);
}

void TestOwnerContextOverridesWriteEventSource()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x7EU};
  const uint8_t second[] = {0x7FU};
  std::array<uint8_t, DMA_BLOCK_SIZE + 1U> oversized{};
  LibXR::WriteOperation first_op;
  LibXR::WriteOperation second_op;
  CallbackProbe oversized_probe;
  LibXR::WriteOperation oversized_op(oversized_probe.callback);
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  backend.BlockNextStart();
  std::thread isr_owner(
      [&]
      {
        backend.Complete(true);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  ASSERT(backend.Write(oversized.data(), oversized.size(), oversized_op, false) ==
         LibXR::ErrorCode::OK);
  backend.ReleaseBlockedStart();
  ASSERT(oversized_probe.WaitForCount(1U));
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  isr_owner.join();
  ASSERT(oversized_probe.in_isr.load(std::memory_order_acquire) == 1U);
  ASSERT(static_cast<LibXR::ErrorCode>(oversized_probe.result.load(
             std::memory_order_acquire)) == LibXR::ErrorCode::FAILED);
}

}  // namespace

void RunConcurrencyTests()
{
  TestConcurrentWriteAndTerminalEventsMakeProgress();
  TestAbortAbsorbsCoalescedTerminalEvents();
  TestStreamPublicationSurvivesAbortBoundary();
  TestOwnerContextOverridesTerminalEventSource();
  TestOwnerContextOverridesWriteEventSource();
}

}  // namespace LibXRTest::UartDmaTx
