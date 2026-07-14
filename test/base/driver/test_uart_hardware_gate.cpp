#include <atomic>
#include <chrono>
#include <thread>

#include "model/uart_hardware_gate.hpp"
#include "test.hpp"

namespace
{

using Gate = LibXR::UartHardwareGate;
using Action = Gate::PendingAction;

void TestConfigRequestBeforeClaimIsConsumed()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  ASSERT(gate.ConfigActive());
  ASSERT(gate.ConfigRequested());
  ASSERT(gate.LeaveConfig() == Action::NONE);
}

void TestConfigRequestAfterClaimRemainsPending()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.RequestConfig();
  ASSERT(gate.ConfigRequested());
  ASSERT(Gate::HasAction(gate.LeaveConfig(), Action::CONFIG));
  ASSERT(gate.TryEnterConfig());
  ASSERT(gate.LeaveConfig() == Action::NONE);
}

void TestActiveConfigMayConsumeRequestBeforeSnapshot()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.RequestConfig();
  gate.ConsumePendingConfig();
  ASSERT(gate.LeaveConfig() == Action::NONE);
  ASSERT(!gate.ConfigRequested());
}

void TestOwnersAreMutuallyExclusive()
{
  Gate gate;
  ASSERT(gate.TryEnterIrq());
  ASSERT(!gate.TryEnterTxStart());
  gate.RequestConfig();
  const Action irq_actions = gate.LeaveIrq();
  ASSERT(Gate::HasAction(irq_actions, Action::CONFIG));
  ASSERT(Gate::HasAction(irq_actions, Action::TX_START));

  ASSERT(gate.TryEnterConfig());
  ASSERT(!gate.TryEnterIrq());
  ASSERT(!gate.TryEnterTxStart());
  ASSERT(gate.LeaveConfig() == Action::TX_START);

  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.TxStartActive());
  ASSERT(!gate.TryEnterIrq());
  ASSERT(gate.LeaveTxStart() == Action::NONE);
}

void TestConfigPendingPreventsNewHardwareEntry()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(!gate.TryEnterIrq());
  ASSERT(!gate.TryEnterTxStart());
  ASSERT(gate.TryEnterConfig());
  ASSERT(gate.LeaveConfig() == Action::TX_START);
}

void TestTxRetryRemainsPersistentUntilClaim()
{
  Gate gate;
  ASSERT(gate.TryEnterIrq());
  ASSERT(!gate.TryEnterTxStart());
  ASSERT(gate.LeaveIrq() == Action::TX_START);

  // A returned action is only a dispatch hint. An intervening owner must still see
  // the persistent level fact if the first dispatcher has not claimed TX start yet.
  ASSERT(gate.TryEnterIrq());
  ASSERT(gate.LeaveIrq() == Action::TX_START);
  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.LeaveTxStart() == Action::NONE);
}

void TestConfigIrqSubOwnerRemainsAvailable()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  ASSERT(!gate.TryEnterConfigIrq());
  gate.OpenConfigIrqAdmission();
  ASSERT(gate.TryEnterConfigIrq());
  ASSERT(!gate.TryEnterConfigIrq());
  ASSERT(gate.ConfigActive());
  gate.LeaveConfigIrq();
  ASSERT(gate.ConfigActive());
  ASSERT(gate.LeaveConfig() == Action::NONE);
}

void TestConfigCloseWaitsForIrqSubOwner()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.OpenConfigIrqAdmission();
  ASSERT(gate.TryEnterConfigIrq());

  Action actions = Action::TX_START;
  ASSERT(!gate.TryLeaveConfig(actions));
  ASSERT(actions == Action::NONE);
  ASSERT(gate.ConfigActive());

  gate.LeaveConfigIrq();
  ASSERT(!gate.TryEnterConfigIrq());
  ASSERT(gate.TryLeaveConfig(actions));
  ASSERT(actions == Action::NONE);
  ASSERT(!gate.ConfigActive());
}

void TestConfigIrqDoesNotConsumeTxRetry()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.OpenConfigIrqAdmission();
  ASSERT(gate.TryEnterConfigIrq());
  ASSERT(!gate.TryEnterTxStart());
  gate.LeaveConfigIrq();
  ASSERT(gate.LeaveConfig() == Action::TX_START);
}

void TestConfigIrqAdmissionPublishesAndJoinsCallbackState()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());

  uint32_t config_payload = 0U;
  uint32_t callback_payload = 0U;
  std::atomic<uint32_t> start{0U};
  std::atomic<uint32_t> entered{0U};
  std::atomic<uint32_t> release_callback{0U};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  std::thread callback(
      [&]
      {
        while (start.load(std::memory_order_acquire) == 0U)
        {
          ASSERT(std::chrono::steady_clock::now() < deadline);
          std::this_thread::yield();
        }
        while (!gate.TryEnterConfigIrq())
        {
          ASSERT(std::chrono::steady_clock::now() < deadline);
          std::this_thread::yield();
        }
        ASSERT(config_payload == 0x12345678U);
        entered.store(1U, std::memory_order_release);
        while (release_callback.load(std::memory_order_acquire) == 0U)
        {
          ASSERT(std::chrono::steady_clock::now() < deadline);
          std::this_thread::yield();
        }
        callback_payload = 0x87654321U;
        gate.LeaveConfigIrq();
      });

  start.store(1U, std::memory_order_release);
  config_payload = 0x12345678U;
  gate.OpenConfigIrqAdmission();
  while (entered.load(std::memory_order_acquire) == 0U)
  {
    ASSERT(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }

  Action actions = Action::NONE;
  ASSERT(!gate.TryLeaveConfig(actions));
  ASSERT(!gate.TryEnterConfigIrq());
  release_callback.store(1U, std::memory_order_release);
  while (!gate.TryLeaveConfig(actions))
  {
    ASSERT(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }

  // TryLeaveConfig's acquire RMW joins the callback's release RMW before this read.
  ASSERT(callback_payload == 0x87654321U);
  callback.join();
}

void TestDeferredIrqSurvivesOwnerRelease()
{
  Gate gate;
  ASSERT(gate.TryEnterTxStart());

  gate.MarkIrqDeferred();
  ASSERT(gate.IrqDeferred());
  ASSERT(!gate.TryEnterDeferredIrq());

  const Action actions = gate.LeaveTxStart();
  ASSERT(Gate::HasAction(actions, Action::IRQ_DEFERRED));
  ASSERT(gate.IrqDeferred());

  ASSERT(gate.TryEnterDeferredIrq());
  ASSERT(!gate.IrqDeferred());
  ASSERT(gate.LeaveIrq() == Action::NONE);
}

void TestDeferredIrqClaimWinsAfterOwnerReleased()
{
  Gate gate;
  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.LeaveTxStart() == Action::NONE);

  gate.MarkIrqDeferred();
  ASSERT(gate.TryEnterDeferredIrq());
  ASSERT(!gate.IrqDeferred());
  ASSERT(gate.LeaveIrq() == Action::NONE);
}

void TestOrdinaryIrqDoesNotConsumeDeferredDomainWork()
{
  Gate gate;
  gate.MarkIrqDeferred();

  ASSERT(gate.TryEnterIrq());
  ASSERT(gate.IrqDeferred());
  const Action ordinary_actions = gate.LeaveIrq();
  ASSERT(Gate::HasAction(ordinary_actions, Action::IRQ_DEFERRED));
  ASSERT(!gate.TryEnterTxStart());

  ASSERT(gate.TryEnterDeferredIrq());
  const Action deferred_actions = gate.LeaveIrq();
  ASSERT(Gate::HasAction(deferred_actions, Action::TX_START));
  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.LeaveTxStart() == Action::NONE);
}

void TestConfigThenDeferredIrqThenTxPriority()
{
  Gate gate;
  gate.MarkIrqDeferred();
  ASSERT(!gate.TryEnterTxStart());
  gate.RequestConfig();

  ASSERT(!gate.TryEnterIrq());
  ASSERT(!gate.TryEnterDeferredIrq());
  ASSERT(gate.TryEnterConfig());
  const Action config_actions = gate.LeaveConfig();
  ASSERT(Gate::HasAction(config_actions, Action::IRQ_DEFERRED));
  ASSERT(Gate::HasAction(config_actions, Action::TX_START));

  // Deferred hardware status keeps TX start blocked until an IRQ-domain owner has
  // scanned and cleared it.
  ASSERT(!gate.TryEnterTxStart());
  ASSERT(gate.TryEnterDeferredIrq());
  const Action irq_actions = gate.LeaveIrq();
  ASSERT(Gate::HasAction(irq_actions, Action::TX_START));

  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.LeaveTxStart() == Action::NONE);
}

void TestBackToBackConfigKeepsDeferredIrqAheadOfTx()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.MarkIrqDeferred();
  gate.RequestConfig();
  ASSERT(!gate.TryEnterTxStart());

  const Action actions = gate.LeaveConfig();
  ASSERT(Gate::HasAction(actions, Action::CONFIG));
  ASSERT(Gate::HasAction(actions, Action::IRQ_DEFERRED));
  ASSERT(Gate::HasAction(actions, Action::TX_START));
  ASSERT(!gate.TryEnterDeferredIrq());

  ASSERT(gate.TryEnterConfig());
  const Action final_config_actions = gate.LeaveConfig();
  ASSERT(Gate::HasAction(final_config_actions, Action::IRQ_DEFERRED));
  ASSERT(Gate::HasAction(final_config_actions, Action::TX_START));
  ASSERT(gate.TryEnterDeferredIrq());
  ASSERT(gate.LeaveIrq() == Action::TX_START);
  ASSERT(gate.TryEnterTxStart());
  ASSERT(gate.LeaveTxStart() == Action::NONE);
}

void TestConfigIrqDoesNotConsumeDeferredIrq()
{
  Gate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.OpenConfigIrqAdmission();
  ASSERT(gate.TryEnterConfigIrq());
  gate.MarkIrqDeferred();
  gate.LeaveConfigIrq();

  const Action actions = gate.LeaveConfig();
  ASSERT(Gate::HasAction(actions, Action::IRQ_DEFERRED));
  ASSERT(gate.TryEnterDeferredIrq());
  ASSERT(gate.LeaveIrq() == Action::NONE);
}

void TestConcurrentOwnersNeverOverlap()
{
  constexpr uint32_t ITERATIONS = 20000U;
  Gate gate;
  std::atomic<uint32_t> active_owners{0U};
  std::atomic<uint32_t> start{0U};
  std::atomic<uint32_t> timed_out{0U};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  auto enter_owner = [&]
  { ASSERT(active_owners.fetch_add(1U, std::memory_order_acq_rel) == 0U); };
  auto leave_owner = [&]
  { ASSERT(active_owners.fetch_sub(1U, std::memory_order_acq_rel) == 1U); };
  auto wait_for_start = [&]
  {
    while (start.load(std::memory_order_acquire) == 0U)
    {
      std::this_thread::yield();
    }
  };

  std::thread irq_thread(
      [&]
      {
        wait_for_start();
        for (uint32_t count = 0U; count < ITERATIONS;)
        {
          if (gate.TryEnterIrq())
          {
            enter_owner();
            leave_owner();
            (void)gate.LeaveIrq();
            ++count;
          }
          else if (std::chrono::steady_clock::now() >= deadline)
          {
            timed_out.store(1U, std::memory_order_release);
            return;
          }
        }
      });

  std::thread tx_thread(
      [&]
      {
        wait_for_start();
        for (uint32_t count = 0U; count < ITERATIONS;)
        {
          if (gate.TryEnterTxStart())
          {
            enter_owner();
            leave_owner();
            (void)gate.LeaveTxStart();
            ++count;
          }
          else if (std::chrono::steady_clock::now() >= deadline)
          {
            timed_out.store(1U, std::memory_order_release);
            return;
          }
        }
      });

  std::thread config_thread(
      [&]
      {
        wait_for_start();
        for (uint32_t count = 0U; count < ITERATIONS;)
        {
          gate.RequestConfig();
          if (gate.TryEnterConfig())
          {
            enter_owner();
            leave_owner();
            (void)gate.LeaveConfig();
            ++count;
          }
          else if (std::chrono::steady_clock::now() >= deadline)
          {
            timed_out.store(1U, std::memory_order_release);
            return;
          }
        }
      });

  start.store(1U, std::memory_order_release);
  irq_thread.join();
  tx_thread.join();
  config_thread.join();
  ASSERT(timed_out.load(std::memory_order_acquire) == 0U);
  ASSERT(active_owners.load(std::memory_order_acquire) == 0U);
}

}  // namespace

void test_uart_hardware_gate()
{
  TestConfigRequestBeforeClaimIsConsumed();
  TestConfigRequestAfterClaimRemainsPending();
  TestActiveConfigMayConsumeRequestBeforeSnapshot();
  TestOwnersAreMutuallyExclusive();
  TestConfigPendingPreventsNewHardwareEntry();
  TestTxRetryRemainsPersistentUntilClaim();
  TestConfigIrqSubOwnerRemainsAvailable();
  TestConfigCloseWaitsForIrqSubOwner();
  TestConfigIrqDoesNotConsumeTxRetry();
  TestConfigIrqAdmissionPublishesAndJoinsCallbackState();
  TestDeferredIrqSurvivesOwnerRelease();
  TestDeferredIrqClaimWinsAfterOwnerReleased();
  TestOrdinaryIrqDoesNotConsumeDeferredDomainWork();
  TestConfigThenDeferredIrqThenTxPriority();
  TestBackToBackConfigKeepsDeferredIrqAheadOfTx();
  TestConfigIrqDoesNotConsumeDeferredIrq();
  TestConcurrentOwnersNeverOverlap();
}
