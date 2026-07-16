#include <assert.h>
#include <pthread.h>

#include "uart_hardware_gate.hpp"

namespace
{

using Gate = LibXR::UartHardwareGate;
using Action = Gate::PendingAction;

Gate race_gate;

void* PublishConfig(void* argument)
{
  static_cast<Gate*>(argument)->RequestConfig();
  return nullptr;
}

void* PublishDeferred(void* argument)
{
  static_cast<Gate*>(argument)->MarkIrqDeferred();
  return nullptr;
}

void CheckNestedAdmissionConsumesExistingRetry()
{
  Gate gate;
  Gate::OwnerContext context;

  assert(gate.TryEnterIrq(context));

  // A normal TX claim cannot displace the IRQ owner and leaves TX_START_PENDING.
  assert(!gate.TryEnterTxStart());

  // The nested CAS admits the same candidate and consumes that durable retry.
  assert(gate.TryEnterNestedTxStart(context));
  assert(gate.IrqActive());
  assert(!gate.TxStartActive());
  gate.LeaveNestedTxStart(context);

  const Action actions = gate.LeaveIrq(context);
  assert(!Gate::HasAction(actions, Action::TX_START));
  assert(!gate.IrqActive());

  // Outer leave invalidates the stack token so the same object can bind a later IRQ.
  assert(gate.TryEnterIrq(context));
  assert(gate.LeaveIrq(context) == Action::NONE);
}

void CheckPriorityBlocksNestedAdmission()
{
  Gate gate;
  Gate::OwnerContext context;

  assert(gate.TryEnterIrq(context));
  gate.RequestConfig();
  gate.MarkIrqDeferred();
  assert(!gate.TryEnterNestedTxStart(context));

  const Action actions = gate.LeaveIrq(context);
  assert(Gate::HasAction(actions, Action::CONFIG));
  assert(Gate::HasAction(actions, Action::IRQ_DEFERRED));
  assert(Gate::HasAction(actions, Action::TX_START));
}

void CheckConfigClaimRetryBoundary()
{
  Gate gate;

  // A retry published before CONFIG admission belongs to the old fixed prefix.
  assert(gate.TryEnterIrq());
  assert(!gate.TryEnterTxStart());
  gate.RequestConfig();
  const Action irq_actions = gate.LeaveIrq();
  assert(Gate::HasAction(irq_actions, Action::CONFIG));
  assert(Gate::HasAction(irq_actions, Action::TX_START));
  assert(gate.TryEnterConfig());
  assert(gate.LeaveConfig() == Action::NONE);

  // A retry published after the claim CAS is later in modification order and survives.
  gate.RequestConfig();
  assert(gate.TryEnterConfig());
  assert(!gate.TryEnterTxStart());
  assert(gate.LeaveConfig() == Action::TX_START);
  assert(gate.TryEnterTxStart());
  assert(gate.LeaveTxStart() == Action::NONE);
}

void CheckPriorityPublicationRace()
{
  Gate::OwnerContext irq_context;
  assert(race_gate.TryEnterIrq(irq_context));

  // Force the optimized initial expected=IRQ_ACTIVE CAS to observe a stale retry and
  // fail once. CONFIG/deferred may then publish before the retry CAS, which must either
  // lose to priority or atomically admit and consume the stale retry.
  assert(!race_gate.TryEnterTxStart());

  pthread_t config{};
  pthread_t deferred{};
  assert(pthread_create(&config, nullptr, PublishConfig, &race_gate) == 0);
  assert(pthread_create(&deferred, nullptr, PublishDeferred, &race_gate) == 0);

  const bool nested = race_gate.TryEnterNestedTxStart(irq_context);
  if (nested)
  {
    race_gate.LeaveNestedTxStart(irq_context);
  }

  const Action irq_actions = race_gate.LeaveIrq(irq_context);
  if (nested)
  {
    // The nested CAS linearized first and consumed any TX retry.
    assert(!Gate::HasAction(irq_actions, Action::TX_START));
  }
  else
  {
    // A high-priority publisher linearized first. RETRY must already be durable before
    // the synchronous IRQ call stack is allowed to release its outer owner.
    assert(Gate::HasAction(irq_actions, Action::TX_START));
  }

  assert(pthread_join(config, nullptr) == 0);
  assert(pthread_join(deferred, nullptr) == 0);

  // Drain in the platform priority order. Publications that occurred after the outer
  // leave may be absent from irq_actions but remain persistent in the gate word.
  assert(race_gate.ConfigRequested());
  assert(race_gate.TryEnterConfig());
  const Action config_actions = race_gate.LeaveConfig();
  assert(!Gate::HasAction(config_actions, Action::TX_START));

  assert(race_gate.IrqDeferred());
  Gate::OwnerContext deferred_context;
  assert(race_gate.TryEnterDeferredIrq(deferred_context));
  const Action deferred_actions = race_gate.LeaveIrq(deferred_context);
  assert(!Gate::HasAction(deferred_actions, Action::TX_START));

  assert(!race_gate.ConfigRequested());
  assert(!race_gate.IrqDeferred());
  assert(!race_gate.IrqActive());
  assert(!race_gate.TxStartActive());
  assert(!race_gate.ConfigActive());
}

}  // namespace

int main()
{
  CheckNestedAdmissionConsumesExistingRetry();
  CheckPriorityBlocksNestedAdmission();
  CheckConfigClaimRetryBoundary();
  CheckPriorityPublicationRace();
  return 0;
}
