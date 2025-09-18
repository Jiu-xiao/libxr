#include "stm32_timebase.hpp"

using namespace LibXR;

STM32Timebase::STM32Timebase()
    : Timebase(static_cast<uint64_t>(UINT32_MAX) * 1000 + 999, UINT32_MAX)
{
}

MicrosecondTimestamp STM32Timebase::_get_microseconds()
{
  uint32_t tick_old = HAL_GetTick();
  uint32_t cnt_old = SysTick->VAL;
  uint32_t tick_new = HAL_GetTick();
  uint32_t cnt_new = SysTick->VAL;

  auto time_diff = tick_new - tick_old;
  uint32_t tick_load = SysTick->LOAD + 1;
  switch (time_diff)
  {
    case 0:
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 + 1000 -
                                  static_cast<uint64_t>(cnt_old) * 1000 /
                                      tick_load);
    case 1:
      /* 中断发生在两次读取之间 / Interrupt happened between two reads */
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 + 1000 -
                                  static_cast<uint64_t>(cnt_new) * 1000 /
                                      tick_load);
    default:
      /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
       * 1ms, an error case */
      ASSERT(false);
  }

  return 0;
}

MillisecondTimestamp STM32Timebase::_get_milliseconds() { return HAL_GetTick(); }

#ifdef HAL_TIM_MODULE_ENABLED

TIM_HandleTypeDef* STM32TimerTimebase::htim = nullptr;

STM32TimerTimebase::STM32TimerTimebase(TIM_HandleTypeDef* timer)
    : Timebase(static_cast<uint64_t>(UINT32_MAX) * 1000 + 999, UINT32_MAX)
{
  htim = timer;
}

MicrosecondTimestamp STM32TimerTimebase::_get_microseconds()
{
  uint32_t tick_old = HAL_GetTick();
  uint32_t cnt_old = __HAL_TIM_GET_COUNTER(htim);
  uint32_t tick_new = HAL_GetTick();
  uint32_t cnt_new = __HAL_TIM_GET_COUNTER(htim);

  uint32_t autoreload = __HAL_TIM_GET_AUTORELOAD(htim) + 1;

  uint32_t delta_ms = tick_new - tick_old;
  switch (delta_ms)
  {
    case 0:
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 +
                                  static_cast<uint64_t>(cnt_old) * 1000 /
                                      autoreload);
    case 1:
      /* 中断发生在两次读取之间 / Interrupt happened between two reads */
      return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000 +
                                  static_cast<uint64_t>(cnt_new) * 1000 /
                                      autoreload);
    default:
      /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
       * 1ms, an error case */
      ASSERT(false);
  }

  return 0;
}

MillisecondTimestamp STM32TimerTimebase::_get_milliseconds() { return HAL_GetTick(); }

#endif
