#include "ch32_timebase.hpp"

using namespace LibXR;

CH32Timebase::CH32Timebase() : Timebase(UINT32_MAX * 1000 + 999, UINT32_MAX) {}

MicrosecondTimestamp CH32Timebase::_get_microseconds()
{
  uint32_t tick_old = sys_tick_ms;
  uint32_t cnt_old = SysTick->CNT;
  uint32_t tick_new = sys_tick_ms;
  uint32_t cnt_new = SysTick->CNT;

  auto tick_diff = tick_new - tick_old;
  uint32_t tick_cmp = SysTick->CMP + 1;
  switch (tick_diff)
  {
    case 0:
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 +
                                  static_cast<uint64_t>(cnt_old) * 1000 / tick_cmp);
    case 1:
      /* 中断发生在两次读取之间 / Interrupt happened between two reads */
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 +
                                  static_cast<uint64_t>(cnt_new) * 1000 / tick_cmp);
    default:
      /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
       * 1ms, an error case */
      ASSERT(false);
  }

  return 0;
}

MillisecondTimestamp CH32Timebase::_get_milliseconds() { return sys_tick_ms; }

void CH32Timebase::OnSysTickInterrupt() { sys_tick_ms++; }

void CH32Timebase::Sync(uint32_t ticks) { sys_tick_ms = ticks; }

extern "C" void libxr_systick_handler(void) { CH32Timebase::OnSysTickInterrupt(); }
