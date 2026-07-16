#include "uart_hardware_gate.hpp"

#include <assert.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>

namespace
{

LibXR::UartHardwareGate gate;
std::atomic<uint32_t> observed_owner{0U};

void EnterObservedOwner(uint32_t owner)
{
  const uint32_t previous = observed_owner.fetch_or(owner, std::memory_order_acq_rel);
  assert(previous == 0U);
}

void LeaveObservedOwner(uint32_t owner)
{
  const uint32_t previous = observed_owner.fetch_and(~owner, std::memory_order_acq_rel);
  assert(previous == owner);
}

void* RunIrq(void*)
{
  constexpr uint32_t owner = 1U << 0U;
  if (gate.TryEnterIrq())
  {
    EnterObservedOwner(owner);
    LeaveObservedOwner(owner);
    (void)gate.LeaveIrq();
    return nullptr;
  }

  gate.MarkIrqDeferred();
  if (gate.TryEnterDeferredIrq())
  {
    EnterObservedOwner(owner);
    LeaveObservedOwner(owner);
    (void)gate.LeaveIrq();
  }
  return nullptr;
}

void* RunTxStart(void*)
{
  constexpr uint32_t owner = 1U << 1U;
  if (gate.TryEnterTxStart())
  {
    EnterObservedOwner(owner);
    LeaveObservedOwner(owner);
    (void)gate.LeaveTxStart();
  }
  return nullptr;
}

void* RunConfig(void*)
{
  constexpr uint32_t owner = 1U << 2U;
  gate.RequestConfig();
  if (gate.TryEnterConfig())
  {
    EnterObservedOwner(owner);
    LeaveObservedOwner(owner);
    (void)gate.LeaveConfig();
  }
  return nullptr;
}

}  // namespace

int main()
{
  pthread_t irq{};
  pthread_t tx_start{};
  pthread_t config{};
  assert(pthread_create(&irq, nullptr, RunIrq, nullptr) == 0);
  assert(pthread_create(&tx_start, nullptr, RunTxStart, nullptr) == 0);
  assert(pthread_create(&config, nullptr, RunConfig, nullptr) == 0);
  assert(pthread_join(irq, nullptr) == 0);
  assert(pthread_join(tx_start, nullptr) == 0);
  assert(pthread_join(config, nullptr) == 0);
  assert(observed_owner.load(std::memory_order_acquire) == 0U);
  assert(!gate.IrqActive());
  assert(!gate.TxStartActive());
  assert(!gate.ConfigActive());
  return 0;
}
