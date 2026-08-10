#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "mspm0_dma_dispatcher.hpp"

namespace
{

#if !defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
extern "C" void DMA_IRQHandler(void);
#endif

using LibXR::ErrorCode;
namespace Dispatcher = LibXR::MSPM0DmaDispatcher;
constexpr IRQn_Type TEST_UART_IRQn = 15;

#define CHECK(condition)                                                           \
  do                                                                               \
  {                                                                                \
    if (!(condition))                                                              \
    {                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " << #condition \
                << '\n';                                                           \
      return false;                                                                \
    }                                                                              \
  } while (false)

struct Observer
{
  std::array<uint32_t, 8U> events{};
  size_t calls = 0U;
  uint32_t reassert_mask = 0U;
  size_t reasserts_remaining = 0U;
  bool reassert_forever = false;
};

bool masked_callback_ran = false;
bool masked_callback_observed_primask = false;
bool unmask_hook_observed_handoff = false;
size_t unmask_hook_calls = 0U;

void ObserveMaskedHandoff(void*, uint32_t)
{
  masked_callback_observed_primask = __get_PRIMASK() != 0U;
  masked_callback_ran = true;
  NVIC_SetPendingIRQ(TEST_UART_IRQn);
}

void ObserveFirstUnmask()
{
  FakeMSPM0::primask_restore_hook = nullptr;
  ++unmask_hook_calls;
  unmask_hook_observed_handoff = masked_callback_ran &&
                                 masked_callback_observed_primask &&
                                 NVIC_GetPendingIRQ(TEST_UART_IRQn) != 0U;
}

void Observe(void* context, uint32_t events)
{
  auto& observer = *static_cast<Observer*>(context);
  if (observer.calls < observer.events.size())
  {
    observer.events[observer.calls] = events;
  }
  ++observer.calls;
  if (observer.reassert_forever || observer.reasserts_remaining != 0U)
  {
    if (observer.reasserts_remaining != 0U)
    {
      --observer.reasserts_remaining;
    }
    FakeMSPM0::RaiseDmaInterrupt(observer.reassert_mask);
  }
}

void ResetHarness()
{
  FakeMSPM0::ResetRuntime();
  FakeMSPM0::ResetDma();
  Dispatcher::ResetDiagnostics();
  masked_callback_ran = false;
  masked_callback_observed_primask = false;
  unmask_hook_observed_handoff = false;
  unmask_hook_calls = 0U;
}

void ServiceDma()
{
  FakeMSPM0::IrqScope scope(DMA_INT_IRQn);
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  Dispatcher::Dispatch();
#else
  DMA_IRQHandler();
#endif
}

bool TestMasksAndCallbackHandoffAreOneCriticalSection()
{
  ResetHarness();
  Dispatcher::Registration registration;

  CHECK(Dispatcher::EarlyMask(8U) == 0U);
  CHECK(Dispatcher::EarlyMask(15U) == 0U);
  CHECK(Dispatcher::EarlyMask(0xFFU) == 0U);
  CHECK(Dispatcher::Register(0U, Dispatcher::COMPLETE, ObserveMaskedHandoff, nullptr,
                             registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);

  FakeMSPM0::primask_restore_hook = ObserveFirstUnmask;
  FakeMSPM0::RaiseDmaInterrupt(Dispatcher::CompleteMask(0U));
  ServiceDma();

  CHECK(masked_callback_ran);
  CHECK(masked_callback_observed_primask);
  CHECK(unmask_hook_calls == 1U);
  CHECK(unmask_hook_observed_handoff);
  CHECK(Dispatcher::Unregister(registration) == ErrorCode::OK);
  return true;
}

bool TestDmaIrqOwnershipMode()
{
  ResetHarness();
  Observer observer;
  Dispatcher::Registration registration;
  NVIC_DisableIRQ(DMA_INT_IRQn);

  CHECK(Dispatcher::Register(0U, Dispatcher::COMPLETE, Observe, &observer,
                             registration) == ErrorCode::OK);
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) == 0U);
#else
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
#endif
  CHECK(Dispatcher::SetEnabled(registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) == 0U);
#else
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
#endif
  CHECK(Dispatcher::Unregister(registration) == ErrorCode::OK);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) == 0U);

  NVIC_EnableIRQ(DMA_INT_IRQn);
  CHECK(Dispatcher::Register(0U, Dispatcher::COMPLETE, Observe, &observer,
                             registration) == ErrorCode::OK);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
  CHECK(Dispatcher::SetEnabled(registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
  CHECK(Dispatcher::Unregister(registration) == ErrorCode::OK);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
  NVIC_DisableIRQ(DMA_INT_IRQn);
  return true;
}

bool TestSimultaneousOwnersAndCombinedSnapshot()
{
  ResetHarness();
  Observer ring;
  Observer channel_11;
  Dispatcher::Registration ring_registration;
  Dispatcher::Registration channel_11_registration;
  const uint32_t ring_events = Dispatcher::EARLY | Dispatcher::COMPLETE;

  CHECK(Dispatcher::CompleteMask(11U) == (1UL << 11U));
  CHECK(Dispatcher::Register(0U, ring_events, Observe, &ring, ring_registration) ==
        ErrorCode::OK);
  CHECK(Dispatcher::Register(11U, Dispatcher::COMPLETE, Observe, &channel_11,
                             channel_11_registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(ring_registration, ring_events, true) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(channel_11_registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);

  const uint32_t snapshot = Dispatcher::EarlyMask(0U) | Dispatcher::CompleteMask(0U) |
                            Dispatcher::CompleteMask(11U);
  FakeMSPM0::RaiseDmaInterrupt(snapshot);
  ServiceDma();

  CHECK(ring.calls == 1U);
  CHECK(ring.events[0U] == ring_events);
  CHECK(channel_11.calls == 1U);
  CHECK(channel_11.events[0U] == Dispatcher::COMPLETE);
  CHECK(FakeMSPM0::dma_clear_history_size >= 1U);
  CHECK(FakeMSPM0::dma_clear_history[FakeMSPM0::dma_clear_history_size - 1U] == snapshot);
  CHECK((DMA->CPU_INT.RIS & snapshot) == 0U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) == 0U);
  CHECK(Dispatcher::GetLastUnclaimedMask() == 0U);
  CHECK(Dispatcher::Unregister(channel_11_registration) == ErrorCode::OK);
  CHECK(Dispatcher::Unregister(ring_registration) == ErrorCode::OK);
  return true;
}

bool TestRegistrationAndTokenValidation()
{
  ResetHarness();
  Observer first;
  Observer second;
  Dispatcher::Registration first_registration;
  Dispatcher::Registration second_registration;

  CHECK(Dispatcher::Register(0U, 0U, Observe, &first, first_registration) ==
        ErrorCode::ARG_ERR);
  CHECK(Dispatcher::Register(0U, Dispatcher::COMPLETE, nullptr, &first,
                             first_registration) == ErrorCode::ARG_ERR);
  CHECK(Dispatcher::Register(12U, Dispatcher::COMPLETE, Observe, &first,
                             first_registration) == ErrorCode::OUT_OF_RANGE);
  CHECK(Dispatcher::Register(6U, Dispatcher::EARLY, Observe, &first,
                             first_registration) == ErrorCode::NOT_SUPPORT);
  CHECK(Dispatcher::Register(1U, Dispatcher::COMPLETE, Observe, &first,
                             first_registration) == ErrorCode::OK);
  CHECK(Dispatcher::Register(1U, Dispatcher::COMPLETE, Observe, &second,
                             second_registration) == ErrorCode::BUSY);
  CHECK(Dispatcher::Register(2U, Dispatcher::COMPLETE, Observe, &second,
                             first_registration) == ErrorCode::STATE_ERR);
  CHECK(Dispatcher::SetEnabled(first_registration, Dispatcher::EARLY, true) ==
        ErrorCode::ARG_ERR);
  CHECK(Dispatcher::Unregister(first_registration) == ErrorCode::OK);
  CHECK(Dispatcher::Unregister(first_registration) == ErrorCode::STATE_ERR);
  CHECK(Dispatcher::SetEnabled(first_registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::STATE_ERR);
  CHECK(!first_registration.IsValid());
  return true;
}

bool TestUnregisterPreservesOtherOwner()
{
  ResetHarness();
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  NVIC_EnableIRQ(DMA_INT_IRQn);
#endif
  Observer removed;
  Observer retained;
  Dispatcher::Registration removed_registration;
  Dispatcher::Registration retained_registration;

  CHECK(Dispatcher::Register(2U, Dispatcher::COMPLETE, Observe, &removed,
                             removed_registration) == ErrorCode::OK);
  CHECK(Dispatcher::Register(3U, Dispatcher::COMPLETE, Observe, &retained,
                             retained_registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(removed_registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(retained_registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);

  const uint32_t removed_cause = Dispatcher::CompleteMask(2U);
  const uint32_t retained_cause = Dispatcher::CompleteMask(3U);
  FakeMSPM0::RaiseDmaInterrupt(removed_cause | retained_cause);
  CHECK(Dispatcher::Unregister(removed_registration) == ErrorCode::OK);
  CHECK((DMA->CPU_INT.RIS & removed_cause) == 0U);
  CHECK((DMA->CPU_INT.RIS & retained_cause) != 0U);
  CHECK((DMA->CPU_INT.IMASK & retained_cause) != 0U);
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);

  ServiceDma();
  CHECK(removed.calls == 0U);
  CHECK(retained.calls == 1U);
  CHECK(Dispatcher::Unregister(retained_registration) == ErrorCode::OK);
#if defined(LIBXR_MSPM0_DMA_EXTERNAL_IRQ_HANDLER)
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) != 0U);
  NVIC_DisableIRQ(DMA_INT_IRQn);
#else
  CHECK(NVIC_GetEnableIRQ(DMA_INT_IRQn) == 0U);
#endif
  return true;
}

bool TestOwnedAndUnclaimedSnapshotClearsOnlyOwnedCause()
{
  ResetHarness();
  Observer owned_observer;
  Dispatcher::Registration owned_registration;
  const uint32_t owned = Dispatcher::CompleteMask(0U);
  const uint32_t unclaimed = Dispatcher::CompleteMask(5U);

  CHECK(Dispatcher::Register(0U, Dispatcher::COMPLETE, Observe, &owned_observer,
                             owned_registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(owned_registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
  DL_DMA_enableInterrupt(DMA, unclaimed);
  const size_t clear_history_before = FakeMSPM0::dma_clear_history_size;

  FakeMSPM0::RaiseDmaInterrupt(owned | unclaimed);
  ServiceDma();

  CHECK(owned_observer.calls == 1U);
  CHECK(owned_observer.events[0U] == Dispatcher::COMPLETE);
  CHECK(FakeMSPM0::dma_clear_history_size == clear_history_before + 1U);
  CHECK(FakeMSPM0::dma_clear_history[clear_history_before] == owned);
  CHECK((DMA->CPU_INT.RIS & owned) == 0U);
  CHECK((DMA->CPU_INT.RIS & unclaimed) != 0U);
  CHECK((DMA->CPU_INT.MIS & unclaimed) != 0U);
  CHECK(Dispatcher::GetLastUnclaimedMask() == unclaimed);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);

  DL_DMA_disableInterrupt(DMA, unclaimed);
  DL_DMA_clearInterruptStatus(DMA, unclaimed);
  NVIC_ClearPendingIRQ(DMA_INT_IRQn);
  CHECK(Dispatcher::Unregister(owned_registration) == ErrorCode::OK);
  return true;
}

bool TestUnclaimedCauseRemainsPending()
{
  ResetHarness();
  const uint32_t unclaimed = Dispatcher::CompleteMask(5U);
  DL_DMA_enableInterrupt(DMA, unclaimed);
  FakeMSPM0::RaiseDmaInterrupt(unclaimed);
  ServiceDma();

  CHECK((DMA->CPU_INT.RIS & unclaimed) != 0U);
  CHECK((DMA->CPU_INT.MIS & unclaimed) != 0U);
  CHECK((FakeMSPM0::dma_cleared_interrupts & unclaimed) == 0U);
  CHECK(Dispatcher::GetLastUnclaimedMask() == unclaimed);
  CHECK(Dispatcher::GetUnclaimedCount() == 1U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);

  DL_DMA_disableInterrupt(DMA, unclaimed);
  DL_DMA_clearInterruptStatus(DMA, unclaimed);
  NVIC_ClearPendingIRQ(DMA_INT_IRQn);
  return true;
}

bool TestReassertAndDrainBound()
{
  ResetHarness();
  Observer observer;
  Dispatcher::Registration registration;
  const uint32_t cause = Dispatcher::CompleteMask(4U);
  observer.reassert_mask = cause;
  observer.reasserts_remaining = 2U;

  CHECK(Dispatcher::Register(4U, Dispatcher::COMPLETE, Observe, &observer,
                             registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(registration, Dispatcher::COMPLETE, true) ==
        ErrorCode::OK);
  FakeMSPM0::RaiseDmaInterrupt(cause);
  ServiceDma();
  CHECK(observer.calls == 3U);
  CHECK((DMA->CPU_INT.RIS & cause) == 0U);
  CHECK(Dispatcher::GetDrainLimitCount() == 0U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) == 0U);

  ServiceDma();
  CHECK(observer.calls == 3U);
  CHECK((DMA->CPU_INT.RIS & cause) == 0U);
  CHECK(Dispatcher::GetDrainLimitCount() == 0U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) == 0U);

  observer.calls = 0U;
  observer.reassert_forever = true;
  FakeMSPM0::RaiseDmaInterrupt(cause);
  ServiceDma();
  CHECK(observer.calls == 4U);
  CHECK(Dispatcher::GetDrainLimitCount() == 1U);
  CHECK((DMA->CPU_INT.RIS & cause) != 0U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);

  observer.reassert_forever = false;
  ServiceDma();
  CHECK((DMA->CPU_INT.RIS & cause) == 0U);
  CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) == 0U);
  CHECK(Dispatcher::Unregister(registration) == ErrorCode::OK);
  return true;
}

bool TestSharedErrorDelivery()
{
  ResetHarness();
  Observer first;
  Observer second;
  Dispatcher::Registration first_registration;
  Dispatcher::Registration second_registration;

  CHECK(Dispatcher::Register(0U, Dispatcher::ERROR, Observe, &first,
                             first_registration) == ErrorCode::OK);
  CHECK(Dispatcher::Register(1U, Dispatcher::ERROR, Observe, &second,
                             second_registration) == ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(first_registration, Dispatcher::ERROR, true) ==
        ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(second_registration, Dispatcher::ERROR, true) ==
        ErrorCode::OK);

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  ServiceDma();
  CHECK(first.calls == 1U && first.events[0U] == Dispatcher::ERROR);
  CHECK(second.calls == 1U && second.events[0U] == Dispatcher::ERROR);
  CHECK((DMA->CPU_INT.RIS & DL_DMA_INTERRUPT_DATA_ERROR) == 0U);

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  size_t clear_history_before = FakeMSPM0::dma_clear_history_size;
  CHECK(Dispatcher::SetEnabled(first_registration, Dispatcher::ERROR, false) ==
        ErrorCode::OK);
  CHECK(FakeMSPM0::dma_clear_history_size == clear_history_before);
  CHECK((DMA->CPU_INT.RIS & DL_DMA_INTERRUPT_DATA_ERROR) != 0U);
  CHECK((DMA->CPU_INT.IMASK & Dispatcher::ErrorMask()) == Dispatcher::ErrorMask());
  ServiceDma();
  CHECK(first.calls == 1U);
  CHECK(second.calls == 2U && second.events[1U] == Dispatcher::ERROR);

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_ADDR_ERROR);
  clear_history_before = FakeMSPM0::dma_clear_history_size;
  CHECK(Dispatcher::SetEnabled(second_registration, Dispatcher::ERROR, false) ==
        ErrorCode::OK);
  CHECK(FakeMSPM0::dma_clear_history_size == clear_history_before + 1U);
  CHECK(FakeMSPM0::dma_clear_history[clear_history_before] == Dispatcher::ErrorMask());
  CHECK((DMA->CPU_INT.RIS & Dispatcher::ErrorMask()) == 0U);
  CHECK((DMA->CPU_INT.IMASK & Dispatcher::ErrorMask()) == 0U);
  ServiceDma();
  CHECK(first.calls == 1U && second.calls == 2U);

  CHECK(Dispatcher::SetEnabled(first_registration, Dispatcher::ERROR, true) ==
        ErrorCode::OK);
  CHECK(Dispatcher::SetEnabled(second_registration, Dispatcher::ERROR, true) ==
        ErrorCode::OK);
  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  clear_history_before = FakeMSPM0::dma_clear_history_size;
  CHECK(Dispatcher::Unregister(second_registration) == ErrorCode::OK);
  CHECK(FakeMSPM0::dma_clear_history_size == clear_history_before);
  CHECK((DMA->CPU_INT.RIS & DL_DMA_INTERRUPT_DATA_ERROR) != 0U);
  CHECK((DMA->CPU_INT.IMASK & Dispatcher::ErrorMask()) == Dispatcher::ErrorMask());
  ServiceDma();
  CHECK(first.calls == 2U && first.events[1U] == Dispatcher::ERROR);
  CHECK(second.calls == 2U);

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_ADDR_ERROR);
  clear_history_before = FakeMSPM0::dma_clear_history_size;
  CHECK(Dispatcher::Unregister(first_registration) == ErrorCode::OK);
  CHECK(FakeMSPM0::dma_clear_history_size == clear_history_before + 1U);
  CHECK(FakeMSPM0::dma_clear_history[clear_history_before] == Dispatcher::ErrorMask());
  CHECK((DMA->CPU_INT.RIS & Dispatcher::ErrorMask()) == 0U);
  CHECK((DMA->CPU_INT.IMASK & Dispatcher::ErrorMask()) == 0U);
  return true;
}

}  // namespace

int main()
{
  if (!TestMasksAndCallbackHandoffAreOneCriticalSection() || !TestDmaIrqOwnershipMode() ||
      !TestSimultaneousOwnersAndCombinedSnapshot() ||
      !TestRegistrationAndTokenValidation() || !TestUnregisterPreservesOtherOwner() ||
      !TestOwnedAndUnclaimedSnapshotClearsOnlyOwnedCause() ||
      !TestUnclaimedCauseRemainsPending() || !TestReassertAndDrainBound() ||
      !TestSharedErrorDelivery())
  {
    return 1;
  }
  std::cout << "MSPM0 DMA dispatcher tests passed\n";
  return 0;
}
