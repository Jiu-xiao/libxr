#include <atomic>
#include <chrono>
#include <thread>

#include "model/uart_irq_config_gate.hpp"
#include "model/uart_rx_config_gate.hpp"
#include "test.hpp"

namespace
{

void TestBasicIrqAndConfigTransitions()
{
  LibXR::UartIrqConfigGate gate;
  ASSERT(gate.TryEnterIrq());
  ASSERT(gate.IrqActive());
  gate.RequestConfig();
  ASSERT(!gate.TryEnterConfig());
  ASSERT(gate.LeaveIrq());
  ASSERT(!gate.IrqActive());
  ASSERT(gate.TryEnterConfig());
  gate.LeaveConfig();
}

void TestConfigIrqEntry()
{
  LibXR::UartIrqConfigGate gate;
  gate.RequestConfig();
  ASSERT(!gate.TryEnterIrq());
  ASSERT(gate.TryEnterConfig());
  ASSERT(!gate.TryEnterIrq());
  ASSERT(gate.TryEnterConfigIrq());
  ASSERT(!gate.TryEnterConfigIrq());
  ASSERT(!gate.LeaveIrq());
  gate.LeaveConfig();
  ASSERT(gate.TryEnterIrq());
  ASSERT(!gate.LeaveIrq());
}

void TestPendingConfigMayBeConsumedByActiveConfig()
{
  LibXR::UartIrqConfigGate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfigIrq());
  ASSERT(gate.LeaveIrq());
  gate.ConsumePendingConfig();
  gate.LeaveConfig();
  ASSERT(gate.TryEnterIrq());
  ASSERT(!gate.LeaveIrq());
}

void TestRxAndIrqConfigClaimsCompose()
{
  LibXR::UartRxConfigGate rx_gate;
  LibXR::UartIrqConfigGate irq_gate;
  ASSERT(irq_gate.TryEnterIrq());

  rx_gate.RequestConfig();
  irq_gate.RequestConfig();
  ASSERT(rx_gate.TryEnterConfig());
  ASSERT(!irq_gate.TryEnterConfig());

  rx_gate.RequestConfig();
  rx_gate.LeaveConfig();
  ASSERT(irq_gate.LeaveIrq());

  ASSERT(rx_gate.TryEnterConfig());
  ASSERT(irq_gate.TryEnterConfig());
  rx_gate.LeaveConfig();
  irq_gate.LeaveConfig();
}

void TestConfigRequestBeforeSnapshotMayBeConsumed()
{
  LibXR::UartIrqConfigGate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());
  gate.RequestConfig();
  gate.ConsumePendingConfig();
  gate.LeaveConfig();
  ASSERT(gate.TryEnterIrq());
  ASSERT(!gate.LeaveIrq());
}

void TestConfigRequestAfterSnapshotRemainsPending()
{
  LibXR::UartIrqConfigGate gate;
  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());

  gate.ConsumePendingConfig();
  gate.RequestConfig();
  gate.LeaveConfig();

  ASSERT(gate.ConfigRequested());
  ASSERT(!gate.TryEnterIrq());
  ASSERT(gate.TryEnterConfig());
  gate.LeaveConfig();
  ASSERT(!gate.ConfigRequested());
}

void TestOldPendingSourcesCannotTerminateNewTransfer()
{
  constexpr uint32_t COMPLETE = 1U << 0U;
  constexpr uint32_t ERROR = 1U << 1U;
  auto run_scenario = [&](uint32_t old_status, uint32_t new_status, uint32_t first_source)
  {
    LibXR::UartIrqConfigGate gate;
    std::atomic<uint32_t> hardware_pending{old_status};
    std::atomic<uint32_t> terminal_count{0U};
    std::atomic<uint32_t> terminal_source{0U};
    std::atomic<uint32_t> consumed_status{0U};

    auto invoke_irq = [&](uint32_t source)
    {
      if (!gate.TryEnterIrq())
      {
        return false;
      }
      const uint32_t status = hardware_pending.exchange(0U, std::memory_order_acq_rel);
      if ((status & (COMPLETE | ERROR)) != 0U)
      {
        terminal_source.store(source, std::memory_order_release);
        consumed_status.store(status, std::memory_order_release);
        terminal_count.fetch_add(1U, std::memory_order_acq_rel);
      }
      (void)gate.LeaveIrq();
      return true;
    };

    gate.RequestConfig();
    ASSERT(gate.TryEnterConfig());

    // A delayed IRQ invocation cannot inspect or clear the old source while CONFIG owns
    // the hardware boundary.
    ASSERT(!invoke_irq(1U));
    ASSERT(hardware_pending.load(std::memory_order_acquire) == old_status);
    ASSERT(terminal_count.load(std::memory_order_acquire) == 0U);

    // Stop/reset and clear happen while CONFIG owns the gate.
    hardware_pending.store(0U, std::memory_order_release);
    gate.LeaveConfig();

    // Retrying the delayed old invocation observes no completion.
    ASSERT(invoke_irq(1U));
    ASSERT(terminal_count.load(std::memory_order_acquire) == 0U);

    // If the new DMA completes before either source runs, either IRQ invocation may
    // consume the new level. Clearing under the unique owner completes it exactly once.
    hardware_pending.fetch_or(new_status, std::memory_order_release);
    const uint32_t second_source = (first_source == 1U) ? 2U : 1U;
    ASSERT(invoke_irq(first_source));
    ASSERT(invoke_irq(second_source));
    ASSERT(terminal_count.load(std::memory_order_acquire) == 1U);
    ASSERT(terminal_source.load(std::memory_order_acquire) == first_source);
    ASSERT(consumed_status.load(std::memory_order_acquire) == new_status);
  };

  run_scenario(COMPLETE, COMPLETE, 1U);
  run_scenario(ERROR, COMPLETE, 2U);
  run_scenario(COMPLETE, ERROR, 2U);
  run_scenario(ERROR, ERROR, 1U);
}

void TestStm32AbortIrqsResumeAfterFinalCallback()
{
  LibXR::UartIrqConfigGate gate;
  std::atomic<uint32_t> remaining_dma_aborts{2U};
  std::atomic<uint32_t> uart_abort_callbacks{0U};
  std::atomic<uint32_t> resume_count{0U};

  gate.RequestConfig();
  ASSERT(gate.TryEnterConfig());

  auto invoke_abort_irq = [&]
  {
    ASSERT(gate.TryEnterConfigIrq());
    const uint32_t previous =
        remaining_dma_aborts.fetch_sub(1U, std::memory_order_acq_rel);
    ASSERT(previous > 0U);
    if (previous == 1U)
    {
      // STM32 HAL raises the UART-level abort callback only after both linked DMA abort
      // callbacks have finished. DmaIRQHandler resumes CONFIG after releasing IRQ owner.
      uart_abort_callbacks.fetch_add(1U, std::memory_order_acq_rel);
    }
    ASSERT(!gate.LeaveIrq());
    if (uart_abort_callbacks.load(std::memory_order_acquire) != 0U)
    {
      resume_count.fetch_add(1U, std::memory_order_acq_rel);
    }
  };

  invoke_abort_irq();
  ASSERT(remaining_dma_aborts.load(std::memory_order_acquire) == 1U);
  ASSERT(uart_abort_callbacks.load(std::memory_order_acquire) == 0U);
  ASSERT(resume_count.load(std::memory_order_acquire) == 0U);

  invoke_abort_irq();
  ASSERT(remaining_dma_aborts.load(std::memory_order_acquire) == 0U);
  ASSERT(uart_abort_callbacks.load(std::memory_order_acquire) == 1U);
  ASSERT(resume_count.load(std::memory_order_acquire) == 1U);
  gate.LeaveConfig();
}

void TestConcurrentIrqSourcesAndConfigHaveOneOwner()
{
  constexpr uint32_t ITERATIONS = 50000U;
  LibXR::UartIrqConfigGate gate;
  std::atomic<uint32_t> irq_entered[2]{};
  std::atomic<uint32_t> irq_left_with_config{0U};
  std::atomic<uint32_t> config_entered{0U};
  std::atomic<uint32_t> owner_count{0U};
  std::atomic<uint32_t> start{0U};
  std::atomic<uint32_t> timed_out{0U};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

  auto irq_worker = [&](uint32_t source)
  {
    while (start.load(std::memory_order_acquire) == 0U)
    {
      std::this_thread::yield();
    }
    while (irq_entered[source].load(std::memory_order_acquire) < ITERATIONS)
    {
      if (std::chrono::steady_clock::now() >= deadline)
      {
        timed_out.store(1U, std::memory_order_release);
        break;
      }
      if (!gate.TryEnterIrq())
      {
        std::this_thread::yield();
        continue;
      }
      ASSERT(owner_count.fetch_add(1U, std::memory_order_acq_rel) == 0U);
      irq_entered[source].fetch_add(1U, std::memory_order_acq_rel);
      ASSERT(owner_count.fetch_sub(1U, std::memory_order_acq_rel) == 1U);
      if (gate.LeaveIrq())
      {
        irq_left_with_config.fetch_add(1U, std::memory_order_acq_rel);
      }
    }
  };

  std::thread irq_threads[] = {std::thread(irq_worker, 0U), std::thread(irq_worker, 1U)};
  std::thread config_thread(
      [&]
      {
        while (start.load(std::memory_order_acquire) == 0U)
        {
          std::this_thread::yield();
        }
        for (uint32_t index = 0U; index < ITERATIONS; ++index)
        {
          gate.RequestConfig();
          while (!gate.TryEnterConfig())
          {
            if (std::chrono::steady_clock::now() >= deadline)
            {
              timed_out.store(1U, std::memory_order_release);
              return;
            }
            std::this_thread::yield();
          }
          ASSERT(owner_count.fetch_add(1U, std::memory_order_acq_rel) == 0U);
          config_entered.fetch_add(1U, std::memory_order_acq_rel);
          ASSERT(owner_count.fetch_sub(1U, std::memory_order_acq_rel) == 1U);
          gate.LeaveConfig();
        }
      });

  start.store(1U, std::memory_order_release);
  for (auto& irq_thread : irq_threads)
  {
    irq_thread.join();
  }
  config_thread.join();
  ASSERT(timed_out.load(std::memory_order_acquire) == 0U);
  ASSERT(irq_entered[0].load(std::memory_order_acquire) == ITERATIONS);
  ASSERT(irq_entered[1].load(std::memory_order_acquire) == ITERATIONS);
  ASSERT(config_entered.load(std::memory_order_acquire) == ITERATIONS);
  ASSERT(owner_count.load(std::memory_order_acquire) == 0U);
  ASSERT(irq_left_with_config.load(std::memory_order_acquire) <= ITERATIONS);
}

}  // namespace

void test_uart_irq_config_gate()
{
  TestBasicIrqAndConfigTransitions();
  TestConfigIrqEntry();
  TestPendingConfigMayBeConsumedByActiveConfig();
  TestRxAndIrqConfigClaimsCompose();
  TestConfigRequestBeforeSnapshotMayBeConsumed();
  TestConfigRequestAfterSnapshotRemainsPending();
  TestOldPendingSourcesCannotTerminateNewTransfer();
  TestStm32AbortIrqsResumeAfterFinalCallback();
  TestConcurrentIrqSourcesAndConfigHaveOneOwner();
}
