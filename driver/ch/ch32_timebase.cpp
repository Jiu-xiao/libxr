// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_timebase.hpp"

#include "ch32_interrupt_guard.h"

using namespace LibXR;

CH32Timebase::CH32Timebase()
{
  ConfigureWrapRange(static_cast<uint64_t>(UINT32_MAX) * 1000ULL + 999ULL, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  const uint32_t interrupt_state = libxr_ch32_interrupt_save_and_disable();
  uint32_t tick = CH32Timebase::sys_tick_ms_;
  uint32_t count = SysTick->CNT;
  if ((SysTick->SR & 1U) != 0U)
  {
    // CNT can reload before the SysTick ISR advances the software epoch.
    ++tick;
    count = SysTick->CNT;
  }
  const uint32_t tick_period = SysTick->CMP + 1U;
  libxr_ch32_interrupt_restore(interrupt_state);

  return MicrosecondTimestamp(static_cast<uint64_t>(tick) * 1000ULL +
                              static_cast<uint64_t>(count) * 1000ULL / tick_period);
}

MillisecondTimestamp Timebase::GetMilliseconds() { return CH32Timebase::sys_tick_ms_; }

void CH32Timebase::OnSysTickInterrupt()
{
  const uint32_t interrupt_state = libxr_ch32_interrupt_save_and_disable();
  ++sys_tick_ms_;
  SysTick->SR = 0U;
  libxr_ch32_interrupt_restore(interrupt_state);
}

void CH32Timebase::Sync(uint32_t ticks) { sys_tick_ms_ = ticks; }

extern "C" void libxr_systick_handler(void) { CH32Timebase::OnSysTickInterrupt(); }

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
