// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_timebase.hpp"

using namespace LibXR;

static inline SysTick_Type* ch32_systick_reg()
{
#if defined(__CH32H417_H)
  return SysTick0;
#else
  return SysTick;
#endif
}

CH32Timebase::CH32Timebase() : Timebase(UINT32_MAX * 1000 + 999, UINT32_MAX) {}

MicrosecondTimestamp CH32Timebase::_get_microseconds()
{
  do
  {
    auto* const SYSTICK = ch32_systick_reg();
    uint32_t tick_old = sys_tick_ms_;
    uint32_t cnt_old = SYSTICK->CNT;
    uint32_t tick_new = sys_tick_ms_;
    uint32_t cnt_new = SYSTICK->CNT;

    auto tick_diff = tick_new - tick_old;
    uint32_t tick_cmp = SYSTICK->CMP + 1;
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
        continue;
    }
  } while (true);
}

MillisecondTimestamp CH32Timebase::_get_milliseconds() { return sys_tick_ms_; }

void CH32Timebase::OnSysTickInterrupt() { sys_tick_ms_++; }

void CH32Timebase::Sync(uint32_t ticks) { sys_tick_ms_ = ticks; }

extern "C" void libxr_systick_handler(void) { CH32Timebase::OnSysTickInterrupt(); }

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
