#include <array>
#include <vector>

#include "uart_dma_tx_model_test_support.hpp"

namespace LibXRTest::UartDmaTx
{
namespace
{

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

void TestConcurrentOperationPublication(bool use_stream)
{
  constexpr uint32_t publication_count = 10000U;
  LibXR::WritePort port(32U, 32U);
  port = [](LibXR::WritePort&, bool) { return LibXR::ErrorCode::PENDING; };
  std::vector<PollStatus> statuses(publication_count, PollStatus::READY);
  std::atomic<uint32_t> consumer_ready{0U};
  std::atomic<uint32_t> consumed{0U};

  std::thread consumer(
      [&]
      {
        consumer_ready.store(1U, std::memory_order_release);
        for (uint32_t index = 0U; index < publication_count; ++index)
        {
          LibXR::WriteInfoBlock info{};
          ASSERT(WaitUntil(
              [&] { return port.queue_info_->Pop(info) == LibXR::ErrorCode::OK; }));
          ASSERT(info.op.type == LibXR::WriteOperation::OperationType::POLLING);
          ASSERT(info.submission_id == index + 1U);
          ASSERT(*info.op.data.status == PollStatus::RUNNING);
          ASSERT(port.queue_data_->PopBatch(nullptr, info.data.size_) ==
                 LibXR::ErrorCode::OK);
          port.Finish(false, LibXR::ErrorCode::OK, info);
          consumed.store(index + 1U, std::memory_order_release);
        }
      });

  ASSERT(WaitUntil([&] { return consumer_ready.load(std::memory_order_acquire) != 0U; }));
  const uint8_t data = 0x60U;
  for (uint32_t index = 0U; index < publication_count; ++index)
  {
    LibXR::WriteOperation op(statuses[index]);
    if (use_stream)
    {
      while (true)
      {
        LibXR::WritePort::Stream stream(&port, op);
        const LibXR::ErrorCode write_result =
            stream.Write(LibXR::ConstRawData(&data, sizeof(data)));
        if (write_result == LibXR::ErrorCode::OK)
        {
          ASSERT(stream.Commit() == LibXR::ErrorCode::OK);
          break;
        }
        ASSERT(write_result == LibXR::ErrorCode::FULL ||
               write_result == LibXR::ErrorCode::BUSY);
        std::this_thread::yield();
      }
    }
    else
    {
      while (true)
      {
        const LibXR::ErrorCode write_result =
            port(LibXR::ConstRawData(&data, sizeof(data)), op, false);
        if (write_result == LibXR::ErrorCode::OK)
        {
          break;
        }
        ASSERT(write_result == LibXR::ErrorCode::FULL ||
               write_result == LibXR::ErrorCode::BUSY);
        std::this_thread::yield();
      }
    }
  }

  ASSERT(WaitUntil(
      [&] { return consumed.load(std::memory_order_acquire) == publication_count; }));
  consumer.join();
  for (PollStatus status : statuses)
  {
    ASSERT(status == PollStatus::DONE);
  }
  ASSERT(port.queue_info_->Size() == 0U);
  ASSERT(port.queue_data_->Size() == 0U);
}

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
  AssertStart(backend, 1U, new_write, sizeof(new_write), 1);
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
  AssertStart(backend, 1U, resubmitted, sizeof(resubmitted), 1);
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
  AssertStart(backend, 1U, new_write, sizeof(new_write), 1);
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

void TestSpuriousConfigResumeDoesNotFreezeWrites()
{
  FakeUartDmaBackend backend;
  const uint8_t data[] = {0x91U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  backend.ResumeConfig(true);
  ASSERT(backend.ConfigApplyCount() == 0U);
  ASSERT(backend.Write(data, sizeof(data), op, false) == LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::DONE);
  AssertStart(backend, 0U, data, sizeof(data), 0);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestSpuriousConfigDoesNotAbsorbCompletion()
{
  FakeUartDmaBackend backend;
  const uint8_t data[] = {0x92U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  backend.CompleteNextStartWithSpuriousConfig();
  ASSERT(backend.Write(data, sizeof(data), op, false) == LibXR::ErrorCode::OK);

  ASSERT(status == PollStatus::DONE);
  ASSERT(backend.ConfigApplyCount() == 0U);
  AssertStart(backend, 0U, data, sizeof(data), 0);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateDefersQueuedStartUntilIrqLeaves()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t data[] = {0xA1U, 0xA2U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  ASSERT(backend.EnterIrqForTest());
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::RUNNING);
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.LeaveIrqForTest(true);

  ASSERT(status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.IsBusy());
  AssertStart(backend, 0U, data, sizeof(data), 0);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestRetryHeadCannotUseLaterSubmitContext()
{
  using Action = LibXR::UartHardwareGate::PendingAction;

  struct ResubmitProbe
  {
    FakeUartDmaBackend* backend = nullptr;
    uint8_t retry_data = 0xA3U;
    LibXR::WriteOperation retry_op{};
    std::atomic<uint32_t> count{0U};
    std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
    std::atomic<uint32_t> retry_result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
    LibXR::WriteOperation::Callback callback =
        LibXR::WriteOperation::Callback::Create(OnComplete, this);

    static void OnComplete(bool in_isr, ResubmitProbe* self, LibXR::ErrorCode result)
    {
      self->result.store(static_cast<uint32_t>(result), std::memory_order_release);
      self->retry_result.store(
          static_cast<uint32_t>(self->backend->Write(
              &self->retry_data, sizeof(self->retry_data), self->retry_op, in_isr)),
          std::memory_order_release);
      self->count.fetch_add(1U, std::memory_order_acq_rel);
    }
  };

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();
  const uint8_t first[] = {0xA0U};
  const uint8_t second[] = {0xA1U, 0xA2U};
  ResubmitProbe first_probe;
  first_probe.backend = &backend;
  CallbackProbe second_probe;
  LibXR::WriteOperation first_op(first_probe.callback);
  LibXR::WriteOperation second_op(second_probe.callback);

  ASSERT(backend.EnterIrqForTest());
  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.StartCount() == 0U);
  const Action actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(actions, Action::TX_START));

  // The later submitter may advance the retained first record, but it must treat that
  // record contextlessly instead of completing it through the later synchronous return.
  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.StartCount() == 1U);
  AssertStart(backend, 0U, first, sizeof(first), 0);
  ASSERT(first_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(first_probe.result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
  ASSERT(first_probe.retry_result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::BUSY));
  ASSERT(second_probe.count.load(std::memory_order_acquire) == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);

  // The saved release hint is now redundant. Re-dispatching it must not double-start
  // or complete either record.
  backend.DispatchActionsForTest(actions, false);
  ASSERT(backend.StartCount() == 1U);
  AssertStart(backend, 0U, first, sizeof(first), 0);
  ASSERT(first_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(first_probe.result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
  ASSERT(second_probe.count.load(std::memory_order_acquire) == 0U);

  backend.Complete(true);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, second, sizeof(second), 1);
  ASSERT(first_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(second_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(second_probe.result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestRetryFailureConfigCallbacksReportProducerBusy()
{
  struct ResubmitProbe
  {
    FakeUartDmaBackend* backend = nullptr;
    uint8_t retry_data = 0U;
    LibXR::WriteOperation retry_op{};
    std::atomic<uint32_t> count{0U};
    std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
    std::atomic<uint32_t> retry_result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
    LibXR::WriteOperation::Callback callback =
        LibXR::WriteOperation::Callback::Create(OnComplete, this);

    static void OnComplete(bool in_isr, ResubmitProbe* self, LibXR::ErrorCode result)
    {
      self->result.store(static_cast<uint32_t>(result), std::memory_order_release);
      self->retry_result.store(
          static_cast<uint32_t>(self->backend->Write(
              &self->retry_data, sizeof(self->retry_data), self->retry_op, in_isr)),
          std::memory_order_release);
      self->count.fetch_add(1U, std::memory_order_acq_rel);
    }
  };

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();
  ASSERT(backend.EnterIrqForTest());

  ResubmitProbe first_probe;
  first_probe.backend = &backend;
  first_probe.retry_data = 0xB3U;
  ResubmitProbe queued_probe;
  queued_probe.backend = &backend;
  queued_probe.retry_data = 0xB4U;
  ResubmitProbe current_probe;
  current_probe.backend = &backend;
  current_probe.retry_data = 0xB5U;

  const uint8_t first = 0xB0U;
  const uint8_t queued = 0xB1U;
  const uint8_t current = 0xB2U;
  LibXR::WriteOperation first_op(first_probe.callback);
  LibXR::WriteOperation queued_op(queued_probe.callback);
  LibXR::WriteOperation current_op(current_probe.callback);

  ASSERT(backend.Write(&first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(&queued, sizeof(queued), queued_op) == LibXR::ErrorCode::OK);
  const auto saved_actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(
      saved_actions, LibXR::UartHardwareGate::PendingAction::TX_START));

  backend.SetStartAllowed(false);
  ASSERT(backend.Write(&current, sizeof(current), current_op) == LibXR::ErrorCode::OK);

  for (ResubmitProbe* probe : {&first_probe, &queued_probe, &current_probe})
  {
    ASSERT(probe->count.load(std::memory_order_acquire) == 1U);
    ASSERT(probe->result.load(std::memory_order_acquire) ==
           static_cast<uint32_t>(LibXR::ErrorCode::FAILED));
    ASSERT(probe->retry_result.load(std::memory_order_acquire) ==
           static_cast<uint32_t>(LibXR::ErrorCode::BUSY));
  }
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.DispatchActionsForTest(saved_actions, false);
  ASSERT(backend.StartCount() == 0U);
}

void TestConfigDrainRetiresStaleTxStartRetry()
{
  using Action = LibXR::UartHardwareGate::PendingAction;

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();
  const uint8_t data[] = {0xB6U, 0xB7U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation operation(status);

  ASSERT(backend.EnterIrqForTest());
  ASSERT(backend.Write(data, sizeof(data), operation) == LibXR::ErrorCode::OK);
  ASSERT(status == PollStatus::RUNNING);
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 1U);
  ASSERT(backend.QueuedDataSize() == 0U);

  backend.Configure(0xC005U, false);
  ASSERT(backend.ConfigApplyCount() == 0U);

  const Action blocked_actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(blocked_actions, Action::CONFIG));
  ASSERT(LibXR::UartHardwareGate::HasAction(blocked_actions, Action::TX_START));
  backend.DispatchActionsForTest(blocked_actions, false);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(status == PollStatus::ERROR);
  ASSERT(backend.StartCount() == 0U);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);

  // CONFIG consumed the retry for the drained candidate. A later unrelated IRQ release
  // must not keep dispatching an empty TX service forever.
  ASSERT(backend.EnterIrqForTest());
  const Action later_actions = backend.TakeIrqActionsForTest();
  ASSERT(!LibXR::UartHardwareGate::HasAction(later_actions, Action::TX_START));
}

void TestTxRetrySurvivesDeferredDispatchInterception()
{
  using Action = LibXR::UartHardwareGate::PendingAction;

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();
  const uint8_t data[] = {0xA3U};
  PollStatus status = PollStatus::READY;
  LibXR::WriteOperation op(status);

  ASSERT(backend.EnterIrqForTest());
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);
  backend.MarkIrqDeferredOnlyForTest();
  const Action first_actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(first_actions, Action::IRQ_DEFERRED));
  ASSERT(LibXR::UartHardwareGate::HasAction(first_actions, Action::TX_START));

  // An ordinary IRQ claims the gate before the releasing owner's dispatcher runs.
  // That dispatcher therefore returns at the deferred-IRQ step before reaching TX.
  ASSERT(backend.EnterIrqForTest());
  backend.DispatchActionsForTest(first_actions, true);
  ASSERT(backend.StartCount() == 0U);
  ASSERT(status == PollStatus::RUNNING);

  // TX_START_PENDING is still in the gate. The intervening owner reports it again,
  // deferred IRQ scanning completes, and only then is the retained write started.
  backend.LeaveIrqForTest(true);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(backend.DeferredRemaskCount() == 1U);
  ASSERT(status == PollStatus::DONE);
  AssertStart(backend, 0U, data, sizeof(data), 0);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateDeferredPublishSelfClaimsAfterOwnerRelease()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t data[] = {0xAAU};
  LibXR::WriteOperation op;
  ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::OK);
  ASSERT(backend.IsBusy());

  ASSERT(backend.EnterIrqForTest());
  backend.BlockNextDeferredPublishForTest();
  std::thread delayed_irq([&] { backend.Complete(true); });
  ASSERT(backend.WaitUntilDeferredPublishBlocked());

  // The owner releases before the losing IRQ publishes its deferred fact, so this
  // Leave cannot hand the work off. The publisher's post-mark claim must close it.
  backend.LeaveIrqForTest(false);
  ASSERT(backend.IsBusy());
  ASSERT(backend.ModelCompleteDispatchCount() == 0U);

  backend.ReleaseDeferredPublishForTest();
  delayed_irq.join();

  ASSERT(backend.ModelCompleteDispatchCount() == 1U);
  ASSERT(backend.DeferredRemaskCount() == 1U);
  ASSERT(!backend.HardwareIrqDeferred());
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateConfigClearsLatchedCompletionBeforeLateMark()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t old_active[] = {0xB1U};
  const uint8_t new_active[] = {0xB2U, 0xB3U};
  LibXR::WriteOperation old_active_op;
  PollStatus new_status = PollStatus::READY;
  LibXR::WriteOperation new_op(new_status);

  ASSERT(backend.Write(old_active, sizeof(old_active), old_active_op) ==
         LibXR::ErrorCode::OK);
  ASSERT(backend.StartCount() == 1U);

  // Model an IRQ that has observed the old terminal status but is preempted
  // before it can publish the deferred gate bit.
  ASSERT(backend.EnterIrqForTest());
  backend.BlockNextDeferredPublishForTest();
  std::thread delayed_irq([&] { backend.Complete(true); });
  ASSERT(backend.WaitUntilDeferredPublishBlocked());
  ASSERT(backend.IrqStatusLatched());
  ASSERT(!backend.IrqDeferredBit());

  backend.LeaveIrqForTest(false);

  // CONFIG owns the hardware while the IRQ's status bit is latched but its
  // deferred marker is not. It must clear the old status before the new TX.
  backend.Configure(0xBADC0DEU, false);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.Write(new_active, sizeof(new_active), new_op, false) ==
         LibXR::ErrorCode::OK);
  ASSERT(new_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.IsBusy());

  // The delayed IRQ can now publish only an empty deferred scan. In particular,
  // it must not interpret the old completion as completion of new_active.
  backend.ReleaseDeferredPublishForTest();
  delayed_irq.join();
  ASSERT(backend.ModelCompleteDispatchCount() == 0U);
  ASSERT(!backend.IrqStatusLatched());
  ASSERT(!backend.IrqDeferredBit());
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  ASSERT(backend.ModelCompleteDispatchCount() == 1U);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateNormalIrqLeavesDeferredForFullRescan()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xABU};
  const uint8_t pending[] = {0xACU};
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation active_op;
  LibXR::WriteOperation pending_op(pending_status);
  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);

  backend.MarkDeferredCompleteForTest();
  ASSERT(backend.HardwareIrqDeferred());
  backend.Complete(true);

  // The ordinary entry only transfers control to the complete-domain deferred scan.
  // It must not dispatch COMPLETE once normally and once again as deferred work.
  ASSERT(backend.ModelCompleteDispatchCount() == 1U);
  ASSERT(!backend.HardwareIrqDeferred());
  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.IsBusy());
  AssertStart(backend, 1U, pending, sizeof(pending), 1);

  backend.Complete(false);
  ASSERT(backend.ModelCompleteDispatchCount() == 2U);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateRetriesDeferredErrorBeforeTxStart()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xADU};
  const uint8_t pending[] = {0xAEU};
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation active_op;
  LibXR::WriteOperation pending_op(pending_status);
  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);

  ASSERT(backend.EnterIrqForTest());
  backend.Error(true);
  ASSERT(backend.HardwareIrqDeferred());
  ASSERT(backend.ModelErrorDispatchCount() == 0U);

  backend.LeaveIrqForTest(true);

  ASSERT(backend.ModelErrorDispatchCount() == 1U);
  ASSERT(!backend.HardwareIrqDeferred());
  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.IsBusy());
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateCoalescedErrorWinsOverCompletion()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xB4U};
  const uint8_t pending[] = {0xB5U};
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation active_op;
  LibXR::WriteOperation pending_op(pending_status);
  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);

  ASSERT(backend.EnterIrqForTest());
  // Both terminal status bits belong to the same hardware snapshot. ERROR must
  // recover once; the co-latched COMPLETE is stale and must be discarded.
  backend.Complete(true);
  backend.Error(true);
  ASSERT(backend.HardwareIrqDeferred());
  backend.LeaveIrqForTest(true);

  ASSERT(backend.ModelErrorDispatchCount() == 1U);
  ASSERT(backend.ModelCompleteDispatchCount() == 0U);
  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  ASSERT(backend.ModelErrorDispatchCount() == 1U);
  ASSERT(backend.ModelCompleteDispatchCount() == 1U);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateNestsPendingPromotionWithinIrqOwner()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xA3U};
  const uint8_t pending[] = {0xA4U, 0xA5U};
  PollStatus active_status = PollStatus::READY;
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation active_op(active_status);
  LibXR::WriteOperation pending_op(pending_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(pending_status == PollStatus::RUNNING);

  ASSERT(backend.EnterIrqForTest());
  backend.CompleteInHeldIrq(true);

  ASSERT(!backend.HardwareIrqDeferred());
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 2U);
  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
  ASSERT(backend.StartAt(1U).nested == 1U);

  const auto actions = backend.TakeIrqActionsForTest();
  ASSERT(!LibXR::UartHardwareGate::HasAction(
      actions, LibXR::UartHardwareGate::PendingAction::TX_START));
  backend.DispatchActionsForTest(actions, true);

  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, pending, sizeof(pending), 1);

  // A duplicate resume is a level-triggered reminder, not a second DMA start.
  backend.ResumeStart(false);
  ASSERT(backend.StartCount() == 2U);
  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestCrossUartCallbackUsesIndependentHardwareOwner()
{
  struct CrossUartProbe
  {
    FakeUartDmaBackend* target = nullptr;
    uint8_t target_data = 0xD3U;
    PollStatus target_status = PollStatus::READY;
    LibXR::WriteOperation target_operation{target_status};
    std::atomic<uint32_t> callback_count{0U};
    std::atomic<uint32_t> submit_result{static_cast<uint32_t>(LibXR::ErrorCode::FAILED)};
    LibXR::WriteOperation::Callback callback =
        LibXR::WriteOperation::Callback::Create(OnComplete, this);

    static void OnComplete(bool in_isr, CrossUartProbe* self, LibXR::ErrorCode result)
    {
      ASSERT(result == LibXR::ErrorCode::OK);
      self->submit_result.store(static_cast<uint32_t>(self->target->Write(
                                    &self->target_data, sizeof(self->target_data),
                                    self->target_operation, in_isr)),
                                std::memory_order_release);
      self->callback_count.fetch_add(1U, std::memory_order_acq_rel);
    }
  };

  FakeUartDmaBackend source;
  FakeUartDmaBackend target;
  source.EnableHardwareGateForTest();
  target.EnableHardwareGateForTest();

  const uint8_t active = 0xD1U;
  const uint8_t pending = 0xD2U;
  LibXR::WriteOperation active_operation;
  CrossUartProbe probe;
  probe.target = &target;
  LibXR::WriteOperation pending_operation(probe.callback);

  ASSERT(source.Write(&active, sizeof(active), active_operation) == LibXR::ErrorCode::OK);
  ASSERT(source.Write(&pending, sizeof(pending), pending_operation) ==
         LibXR::ErrorCode::OK);

  // Source promotes its pending block under the source IRQ context. The callback's
  // target Write receives no source context and must claim the target gate normally.
  source.Complete(true);

  ASSERT(probe.callback_count.load(std::memory_order_acquire) == 1U);
  ASSERT(probe.submit_result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
  ASSERT(probe.target_status == PollStatus::DONE);
  ASSERT(source.StartCount() == 2U);
  ASSERT(source.StartAt(1U).nested == 1U);
  ASSERT(target.StartCount() == 1U);
  ASSERT(target.StartAt(0U).nested == 0U);

  source.Complete(false);
  target.Complete(false);
  ASSERT(!source.IsBusy());
  ASSERT(!target.IsBusy());
}

void TestHardwareGateDeferredPriorityBlocksNestedPromotion()
{
  using Action = LibXR::UartHardwareGate::PendingAction;

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xC1U};
  const uint8_t pending[] = {0xC2U, 0xC3U};
  LibXR::WriteOperation active_op;
  PollStatus pending_status = PollStatus::READY;
  LibXR::WriteOperation pending_op(pending_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(pending, sizeof(pending), pending_op) == LibXR::ErrorCode::OK);

  ASSERT(backend.EnterIrqForTest());
  backend.MarkIrqDeferredOnlyForTest();
  backend.CompleteInHeldIrq(true);

  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  ASSERT(pending_status == PollStatus::RUNNING);
  ASSERT(backend.QueuedInfoCount() == 1U);

  const Action actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(actions, Action::IRQ_DEFERRED));
  ASSERT(LibXR::UartHardwareGate::HasAction(actions, Action::TX_START));
  backend.DispatchActionsForTest(actions, true);

  ASSERT(backend.DeferredRemaskCount() == 1U);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.StartAt(1U).nested == 0U);
  ASSERT(pending_status == PollStatus::DONE);
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestLosingIrqOwnerContextIsNotStoredInCoalescedEvent()
{
  using Action = LibXR::UartHardwareGate::PendingAction;

  struct BlockingProbe
  {
    std::atomic<uint32_t> entered{0U};
    std::atomic<uint32_t> release{0U};
    std::atomic<uint32_t> count{0U};
    std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::PENDING)};
    LibXR::WriteOperation::Callback callback =
        LibXR::WriteOperation::Callback::Create(OnComplete, this);

    static void OnComplete(bool, BlockingProbe* self, LibXR::ErrorCode result)
    {
      self->result.store(static_cast<uint32_t>(result), std::memory_order_release);
      self->entered.store(1U, std::memory_order_release);
      ASSERT(
          WaitUntil([&] { return self->release.load(std::memory_order_acquire) != 0U; }));
      self->count.fetch_add(1U, std::memory_order_acq_rel);
    }
  } first_probe;

  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t first[] = {0xD1U};
  const uint8_t second[] = {0xD2U};
  LibXR::WriteOperation first_op(first_probe.callback);
  PollStatus second_status = PollStatus::READY;
  LibXR::WriteOperation second_op(second_status);
  std::atomic<uint32_t> second_result{static_cast<uint32_t>(LibXR::ErrorCode::PENDING)};

  // This Write has no explicit hardware context, so the held IRQ forces RETRY and leaves
  // a durable TX action. Keep that returned action undispatched for the stale-hint check.
  ASSERT(backend.EnterIrqForTest());
  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  const Action stale_actions = backend.TakeIrqActionsForTest();
  ASSERT(LibXR::UartHardwareGate::HasAction(stale_actions, Action::TX_START));

  std::thread service_owner(
      [&]
      {
        const LibXR::ErrorCode result =
            backend.Write(second, sizeof(second), second_op, false);
        second_result.store(static_cast<uint32_t>(result), std::memory_order_release);
      });
  ASSERT(WaitUntil(
      [&] { return first_probe.entered.load(std::memory_order_acquire) != 0U; }));

  // The IRQ publishes COMPLETE but loses SerializedService ownership to the blocked
  // writer. Its context is invalidated on outer leave and must not travel with the bit.
  ASSERT(backend.EnterIrqForTest());
  backend.CompleteInHeldIrq(true);
  const Action completion_actions = backend.TakeIrqActionsForTest();
  ASSERT(!LibXR::UartHardwareGate::HasAction(completion_actions, Action::TX_START));

  first_probe.release.store(1U, std::memory_order_release);
  service_owner.join();

  ASSERT(first_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(first_probe.result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
  ASSERT(second_result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
  ASSERT(second_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  ASSERT(backend.StartAt(0U).nested == 0U);
  ASSERT(backend.StartAt(1U).nested == 0U);

  // The action captured before the first start is now stale. Replaying it cannot start
  // either accepted record again.
  backend.DispatchActionsForTest(stale_actions, false);
  backend.DispatchActionsForTest(stale_actions, true);
  ASSERT(backend.StartCount() == 2U);

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateConfigPriorityPreservesPostBoundaryWrite()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xA6U};
  const uint8_t old_pending[] = {0xA7U};
  const uint8_t new_write[] = {0xA8U, 0xA9U};
  PollStatus active_status = PollStatus::READY;
  PollStatus old_pending_status = PollStatus::READY;
  PollStatus new_write_status = PollStatus::READY;
  LibXR::WriteOperation active_op(active_status);
  LibXR::WriteOperation old_pending_op(old_pending_status);
  LibXR::WriteOperation new_write_op(new_write_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.Write(old_pending, sizeof(old_pending), old_pending_op) ==
         LibXR::ErrorCode::OK);
  ASSERT(backend.StartCount() == 1U);
  ASSERT(old_pending_status == PollStatus::RUNNING);

  // CONFIG wins before the IRQ-owned service can admit a nested TX start. It fixes the
  // old metadata prefix while hardware application waits for the outer IRQ owner.
  ASSERT(backend.EnterIrqForTest());
  backend.Configure(0xAABBCCDDU, false);
  backend.CompleteInHeldIrq(true);
  // The fixed CONFIG boundary retires this hardware generation. Its terminal reminder
  // cannot release active or promote pending before CONFIG owns and quiesces hardware.
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  ASSERT(old_pending_status == PollStatus::RUNNING);

  // A late terminal for the old hardware generation is deferred behind the IRQ owner.
  // CONFIG must absorb it before the post-boundary write becomes the new active DMA.
  backend.Complete(true);
  ASSERT(backend.HardwareIrqDeferred());
  ASSERT(backend.ModelCompleteDispatchCount() == 1U);

  // The write appended after the fixed CONFIG boundary must survive the old-prefix
  // drain when the outer IRQ owner releases.
  ASSERT(backend.Write(new_write, sizeof(new_write), new_write_op, true) ==
         LibXR::ErrorCode::OK);
  ASSERT(new_write_status == PollStatus::RUNNING);
  ASSERT(backend.QueuedInfoCount() == 2U);

  backend.LeaveIrqForTest(true);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 0xAABBCCDDU);
  ASSERT(backend.ConfigInIsr() == 1U);
  ASSERT(backend.ModelCompleteDispatchCount() == 1U);
  ASSERT(!backend.HardwareIrqDeferred());
  ASSERT(active_status == PollStatus::DONE);
  ASSERT(old_pending_status == PollStatus::ERROR);
  ASSERT(new_write_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, new_write, sizeof(new_write), 1);
  ASSERT(backend.QueuedInfoCount() == 0U);
  ASSERT(backend.QueuedDataSize() == 0U);
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateIdleConfigAppliesImmediately()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  backend.Configure(0xC001U, false);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 0xC001U);
  ASSERT(backend.RxStopCount() == 1U);
  ASSERT(backend.RxRestartCount() == 1U);
  ASSERT(backend.RxRunning());
}

void TestHardwareGateBusyConfigWaitsForFinalTerminal()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xE1U};
  CallbackProbe active_probe;
  LibXR::WriteOperation active_op(active_probe.callback);
  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.IsBusy());

  backend.Configure(0xC002U, false);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 0xC002U);
  ASSERT(backend.ConfigInIsr() == 0U);
  ASSERT(backend.RxStopCount() == 1U);
  ASSERT(backend.RxRestartCount() == 1U);
  ASSERT(backend.RxRunning());
  ASSERT(!backend.IsBusy());
  ASSERT(active_probe.count.load(std::memory_order_acquire) == 1U);
  ASSERT(active_probe.result.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(LibXR::ErrorCode::OK));
}

void TestHardwareGateDmaTcIntermediateDoesNotApplyConfig()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xE2U};
  LibXR::WriteOperation active_op;
  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  ASSERT(backend.IsBusy());

  backend.Configure(0xC003U, false);
  backend.DmaTcIntermediateDuringConfig(true);

  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(backend.AppliedConfig() == 0xC003U);
  ASSERT(backend.RxStopCount() == 1U);
  ASSERT(backend.RxStopCount() == 1U);
  ASSERT(backend.RxRestartCount() == 1U);
  ASSERT(!backend.IsBusy());
}

void TestHardwareGateWriteDuringConfigWaitStartsAfterRelease()
{
  FakeUartDmaBackend backend;
  backend.EnableHardwareGateForTest();

  const uint8_t active[] = {0xE3U};
  const uint8_t next[] = {0xE4U, 0xE5U};
  LibXR::WriteOperation active_op;
  PollStatus next_status = PollStatus::READY;
  LibXR::WriteOperation next_op(next_status);

  ASSERT(backend.Write(active, sizeof(active), active_op) == LibXR::ErrorCode::OK);
  backend.Configure(0xC004U, false);

  ASSERT(backend.Write(next, sizeof(next), next_op, true) == LibXR::ErrorCode::OK);
  ASSERT(backend.ConfigApplyCount() == 1U);
  ASSERT(next_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, next, sizeof(next), 1);
  ASSERT(backend.IsBusy());

  backend.Complete(false);
  ASSERT(!backend.IsBusy());
}

}  // namespace

void RunConcurrencyTests()
{
  TestConcurrentOperationPublication(false);
  TestConcurrentOperationPublication(true);
  TestConcurrentWriteAndTerminalEventsMakeProgress();
  TestConfigAbsorbsCoalescedTerminalEvents();
  TestStreamPublicationSurvivesConfigBoundary();
  TestOwnerContextOverridesTerminalEventSource();
  TestWritesPublishedDuringConfigSurviveFixedPrefix();
  TestCoalescedConfigUsesLatestSnapshot();
  TestConfigCallbackResubmitSurvivesFixedPrefix();
  TestDeferredConfigKeepsOriginalPrefixBoundary();
  TestBackToBackConfigAbsorbsInterveningWrite();
  TestSynchronousConfigResumeIsFlattened();
  TestDeferredConfigResumeUsesLatestSnapshot();
  TestSpuriousConfigResumeDoesNotFreezeWrites();
  TestSpuriousConfigDoesNotAbsorbCompletion();
  TestHardwareGateDefersQueuedStartUntilIrqLeaves();
  TestRetryHeadCannotUseLaterSubmitContext();
  TestRetryFailureConfigCallbacksReportProducerBusy();
  TestConfigDrainRetiresStaleTxStartRetry();
  TestTxRetrySurvivesDeferredDispatchInterception();
  TestHardwareGateDeferredPublishSelfClaimsAfterOwnerRelease();
  TestHardwareGateConfigClearsLatchedCompletionBeforeLateMark();
  TestHardwareGateNormalIrqLeavesDeferredForFullRescan();
  TestHardwareGateRetriesDeferredErrorBeforeTxStart();
  TestHardwareGateCoalescedErrorWinsOverCompletion();
  TestHardwareGateNestsPendingPromotionWithinIrqOwner();
  TestCrossUartCallbackUsesIndependentHardwareOwner();
  TestHardwareGateDeferredPriorityBlocksNestedPromotion();
  TestLosingIrqOwnerContextIsNotStoredInCoalescedEvent();
  TestHardwareGateConfigPriorityPreservesPostBoundaryWrite();
  TestHardwareGateIdleConfigAppliesImmediately();
  TestHardwareGateBusyConfigWaitsForFinalTerminal();
  TestHardwareGateDmaTcIntermediateDoesNotApplyConfig();
  TestHardwareGateWriteDuringConfigWaitStartsAfterRelease();
}

}  // namespace LibXRTest::UartDmaTx
