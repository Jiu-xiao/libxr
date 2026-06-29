#include "mspm0_timebase.hpp"

using namespace LibXR;

MSPM0Timebase::MSPM0Timebase()
{
  ConfigureWrapRange(static_cast<uint64_t>(UINT32_MAX) * 1000ULL + 999ULL, UINT32_MAX);
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  do
  {
    uint32_t tick_old = MSPM0Timebase::sys_tick_ms;
    uint32_t val_old = DL_SYSTICK_getValue();
    uint32_t tick_new = MSPM0Timebase::sys_tick_ms;
    uint32_t val_new = DL_SYSTICK_getValue();

    auto tick_diff = tick_new - tick_old;

    uint32_t cycles_per_ms = DL_SYSTICK_getPeriod();

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
        /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more
         * than 1ms, an error case */
        continue;
    }
  } while (true);
}

MillisecondTimestamp Timebase::GetMilliseconds() { return MSPM0Timebase::sys_tick_ms; }
void MSPM0Timebase::OnSysTickInterrupt() { MSPM0Timebase::sys_tick_ms++; }
void MSPM0Timebase::Sync(uint32_t ticks) { MSPM0Timebase::sys_tick_ms = ticks; }

extern "C" void SysTick_Handler(void)  // NOLINT
{
  LibXR::MSPM0Timebase::OnSysTickInterrupt();
}
