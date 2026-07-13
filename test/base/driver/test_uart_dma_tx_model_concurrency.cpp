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

void TestConfigAbsorbsCoalescedTerminalEvents()
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
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(!backend.IsBusy());
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestStreamPublicationSurvivesConfigBoundary()
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

void TestWritesPublishedDuringConfigSurviveFixedPrefix()
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x81U};
  const uint8_t old_pending[] = {0x82U};
  const uint8_t new_write[] = {0x83U};
  PollStatus statuses[] = {PollStatus::READY, PollStatus::READY, PollStatus::READY};
  LibXR::WriteOperation ops[] = {LibXR::WriteOperation(statuses[0]),
                                 LibXR::WriteOperation(statuses[1]),
                                 LibXR::WriteOperation(statuses[2])};
  std::atomic<uint32_t> config_done{0U};

  ASSERT(backend.Write(active, sizeof(active), ops[0]) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_pending, sizeof(old_pending), ops[1]) == LibXR::ErrorCode::OK);

  backend.BlockNextConfig();
  std::thread owner(
      [&]
      {
        backend.Configure(7U, false);
        config_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilConfigBlocked());

  ASSERT(backend.Write(new_write, sizeof(new_write), ops[2], true) ==
         LibXR::ErrorCode::OK);
  backend.ReleaseBlockedConfig();
  ASSERT(WaitUntil([&] { return config_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(statuses[0] == PollStatus::DONE);
  ASSERT(statuses[1] == PollStatus::ERROR);
  ASSERT(statuses[2] == PollStatus::DONE);
  ASSERT(backend.AppliedConfig() == 7U);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.ConfigInIsr() == 0U);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, new_write, sizeof(new_write), 0);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestCoalescedConfigUsesLatestSnapshot()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {0x84U};
  const uint8_t second[] = {0x85U};
  const uint8_t old_queued[] = {0x86U};
  LibXR::WriteOperation first_op;
  LibXR::WriteOperation second_op;
  CallbackProbe old_queued_probe;
  LibXR::WriteOperation old_queued_op(old_queued_probe.callback);
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_queued, sizeof(old_queued), old_queued_op) ==
         LibXR::ErrorCode::OK);
  backend.BlockNextStart();
  std::thread owner(
      [&]
      {
        backend.Complete(true);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilStartBlocked());

  backend.Configure(11U, false);
  backend.Configure(12U, false);
  backend.ReleaseBlockedStart();
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(backend.AppliedConfig() == 12U);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.ConfigInIsr() == 1U);
  ASSERT(old_queued_probe.WaitForCount(1U));
  ASSERT(old_queued_probe.in_isr.load(std::memory_order_acquire) == 1U);
  ASSERT(static_cast<LibXR::ErrorCode>(old_queued_probe.result.load(
             std::memory_order_acquire)) == LibXR::ErrorCode::FAILED);
}

void TestConfigCallbackResubmitSurvivesFixedPrefix()
{
  struct ResubmitProbe
  {
    FakeUartDmaBackend* backend = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0U;
    PollStatus resubmit_status = PollStatus::READY;
    LibXR::WriteOperation resubmit_op{resubmit_status};
    std::atomic<uint32_t> callback_count{0U};
    std::atomic<uint32_t> callback_in_isr{UINT32_MAX};
    std::atomic<uint32_t> submit_result{static_cast<uint32_t>(LibXR::ErrorCode::FAILED)};
    LibXR::WriteOperation::Callback callback =
        LibXR::WriteOperation::Callback::Create(OnComplete, this);

    static void OnComplete(bool in_isr, ResubmitProbe* self, LibXR::ErrorCode result)
    {
      ASSERT(result == LibXR::ErrorCode::FAILED);
      self->callback_in_isr.store(in_isr ? 1U : 0U, std::memory_order_release);
      self->submit_result.store(static_cast<uint32_t>(self->backend->Write(
                                    self->data, self->size, self->resubmit_op, in_isr)),
                                std::memory_order_release);
      self->callback_count.fetch_add(1U, std::memory_order_acq_rel);
    }
  };

  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x87U};
  const uint8_t old_pending[] = {0x88U};
  const uint8_t old_queued[] = {0x89U};
  const uint8_t resubmitted[] = {0x8AU};
  LibXR::WriteOperation active_op;
  ResubmitProbe pending_probe{&backend, resubmitted, sizeof(resubmitted)};
  LibXR::WriteOperation pending_op(pending_probe.callback);
  PollStatus old_queued_status = PollStatus::READY;
  LibXR::WriteOperation old_queued_op(old_queued_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_pending, sizeof(old_pending), pending_op) ==
         LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_queued, sizeof(old_queued), old_queued_op) ==
         LibXR::ErrorCode::OK);

  backend.Configure(13U, true);

  ASSERT(pending_probe.callback_count.load(std::memory_order_acquire) == 1U);
  ASSERT(pending_probe.callback_in_isr.load(std::memory_order_acquire) == 1U);
  ASSERT(static_cast<LibXR::ErrorCode>(pending_probe.submit_result.load(
             std::memory_order_acquire)) == LibXR::ErrorCode::OK);
  ASSERT(old_queued_status == PollStatus::ERROR);
  ASSERT(pending_probe.resubmit_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, resubmitted, sizeof(resubmitted), 0);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestDeferredConfigKeepsOriginalPrefixBoundary()
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x8BU};
  const uint8_t old_pending[] = {0x8CU};
  const uint8_t new_write[] = {0x8DU};
  PollStatus active_status = PollStatus::READY;
  PollStatus old_pending_status = PollStatus::READY;
  PollStatus new_write_status = PollStatus::READY;
  LibXR::WriteOperation active_op(active_status);
  LibXR::WriteOperation old_pending_op(old_pending_status);
  LibXR::WriteOperation new_write_op(new_write_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_pending, sizeof(old_pending), old_pending_op) ==
         LibXR::ErrorCode::OK);

  backend.SetConfigApplyAllowed(false);
  backend.Configure(14U, false);
  ASSERT(backend.ConfigApplyCount() == 0U);

  ASSERT(backend.Write(new_write, sizeof(new_write), new_write_op, true) ==
         LibXR::ErrorCode::OK);
  ASSERT(new_write_status == PollStatus::RUNNING);

  backend.SetConfigApplyAllowed(true);
  backend.RetryConfig(true);

  ASSERT(active_status == PollStatus::DONE);
  ASSERT(old_pending_status == PollStatus::ERROR);
  ASSERT(new_write_status == PollStatus::DONE);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.ConfigInIsr() == 1U);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, new_write, sizeof(new_write), 0);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
}

void TestBackToBackConfigAbsorbsInterveningWrite()
{
  FakeUartDmaBackend backend;
  const uint8_t active[] = {0x8EU};
  const uint8_t old_pending[] = {0x8FU};
  const uint8_t new_write[] = {0x90U};
  PollStatus old_pending_status = PollStatus::READY;
  PollStatus new_write_status = PollStatus::READY;
  LibXR::WriteOperation active_op;
  LibXR::WriteOperation old_pending_op(old_pending_status);
  LibXR::WriteOperation new_write_op(new_write_status);
  std::atomic<uint32_t> owner_done{0U};

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_pending, sizeof(old_pending), old_pending_op) ==
         LibXR::ErrorCode::OK);

  backend.BlockNextConfig();
  std::thread owner(
      [&]
      {
        backend.Configure(15U, false);
        owner_done.store(1U, std::memory_order_release);
      });
  ASSERT(backend.WaitUntilConfigBlocked());

  backend.Configure(16U, true);
  ASSERT(backend.Write(new_write, sizeof(new_write), new_write_op, true) ==
         LibXR::ErrorCode::OK);
  backend.ReleaseBlockedConfig();
  ASSERT(WaitUntil([&] { return owner_done.load(std::memory_order_acquire) != 0U; }));
  owner.join();

  ASSERT(backend.ConfigApplyCount() == 2U);
  ASSERT(backend.AppliedConfig() == 16U);
  ASSERT(old_pending_status == PollStatus::ERROR);
  ASSERT(new_write_status == PollStatus::ERROR);
  ASSERT(backend.StartCount() == 1U);
}

void TestSynchronousConfigResumeIsFlattened()
{
  FakeUartDmaBackend backend;
  backend.ResumeNextConfigSynchronously();

  backend.Configure(17U, false);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 17U);
  ASSERT(backend.ConfigInIsr() == 0U);
}

void TestDeferredConfigResumeUsesLatestSnapshot()
{
  FakeUartDmaBackend backend;
  backend.SetConfigApplyAllowed(false);

  backend.Configure(18U, false);
  backend.Configure(19U, true);
  ASSERT(backend.ConfigApplyCount() == 0U);

  backend.SetConfigApplyAllowed(true);
  backend.ResumeConfig(true);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 19U);
  ASSERT(backend.ConfigInIsr() == 1U);
}

}  // namespace

void RunConcurrencyTests()
{
  TestConcurrentWriteAndTerminalEventsMakeProgress();
  TestConfigAbsorbsCoalescedTerminalEvents();
  TestStreamPublicationSurvivesConfigBoundary();
  TestOwnerContextOverridesTerminalEventSource();
  TestOwnerContextOverridesWriteEventSource();
  TestWritesPublishedDuringConfigSurviveFixedPrefix();
  TestCoalescedConfigUsesLatestSnapshot();
  TestConfigCallbackResubmitSurvivesFixedPrefix();
  TestDeferredConfigKeepsOriginalPrefixBoundary();
  TestBackToBackConfigAbsorbsInterveningWrite();
  TestSynchronousConfigResumeIsFlattened();
  TestDeferredConfigResumeUsesLatestSnapshot();
}

}  // namespace LibXRTest::UartDmaTx
