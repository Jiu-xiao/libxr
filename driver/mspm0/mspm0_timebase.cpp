#include "mspm0_timebase.hpp"

using namespace LibXR;

MSPM0Timebase::MSPM0Timebase()
    : Timebase(static_cast<uint64_t>(UINT32_MAX) * 1000 + 999, UINT32_MAX)
{
}

MicrosecondTimestamp MSPM0Timebase::_get_microseconds()
{
  do
  {
    uint32_t tick_old = sys_tick_ms;
    uint32_t val_old = DL_SYSTICK_getValue();
    uint32_t tick_new = sys_tick_ms;
    uint32_t val_new = DL_SYSTICK_getValue();

    auto tick_diff = tick_new - tick_old;

    uint32_t cycles_per_ms = DL_SYSTICK_getPeriod() + 1;

    switch (tick_diff)
    {
      case 0:
        return MicrosecondTimestamp(
            static_cast<uint64_t>(tick_new) * 1000 +
            static_cast<uint64_t>(DL_SYSTICK_getPeriod() - val_old) * 1000 /
                cycles_per_ms);
      case 1:
        /* 中断发生在两次读取之间 / Interrupt happened between two reads */
        return MicrosecondTimestamp(
            static_cast<uint64_t>(tick_new) * 1000 +
            static_cast<uint64_t>(DL_SYSTICK_getPeriod() - val_new) * 1000 /
                cycles_per_ms);
      default:
        /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
         * 1ms, an error case */
        continue;
    }
  } while (true);
}

MillisecondTimestamp MSPM0Timebase::_get_milliseconds() { return sys_tick_ms; }
void MSPM0Timebase::OnSysTickInterrupt() { sys_tick_ms++; }
void MSPM0Timebase::Sync(uint32_t ticks) { sys_tick_ms = ticks; }

extern "C" void SysTick_Handler(void)  // NOLINT
{
  LibXR::MSPM0Timebase::OnSysTickInterrupt();
}