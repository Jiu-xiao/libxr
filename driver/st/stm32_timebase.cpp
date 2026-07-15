#include "stm32_timebase.hpp"

using namespace LibXR;

namespace
{
enum class STM32TimebaseBackend : uint8_t
{
  SYSTICK = 0,
  TIMER = 1,
};

STM32TimebaseBackend g_backend = STM32TimebaseBackend::SYSTICK;

MicrosecondTimestamp GetSysTickMicroseconds()
{
  do
  {
    uint32_t tick_old = HAL_GetTick();
    uint32_t cnt_old = SysTick->VAL;
    uint32_t tick_new = HAL_GetTick();
    uint32_t cnt_new = SysTick->VAL;

    const auto time_diff = tick_new - tick_old;
    const uint32_t tick_load = SysTick->LOAD + 1U;
    switch (time_diff)
    {
      case 0:
        return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000ULL + 1000ULL -
                                    static_cast<uint64_t>(cnt_old) * 1000ULL / tick_load);
      case 1:
        return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000ULL + 1000ULL -
                                    static_cast<uint64_t>(cnt_new) * 1000ULL / tick_load);
      default:
        continue;
    }
  } while (true);
}

#ifdef HAL_TIM_MODULE_ENABLED
MicrosecondTimestamp GetTimerMicroseconds(TIM_HandleTypeDef* htim)
{
  ASSERT(htim != nullptr);

  do
  {
    uint32_t tick_old = HAL_GetTick();
    uint32_t cnt_old = __HAL_TIM_GET_COUNTER(htim);
    uint32_t tick_new = HAL_GetTick();
    uint32_t cnt_new = __HAL_TIM_GET_COUNTER(htim);

    const uint32_t autoreload = __HAL_TIM_GET_AUTORELOAD(htim) + 1U;
    const uint32_t delta_ms = tick_new - tick_old;
    switch (delta_ms)
    {
      case 0:
        return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000ULL +
                                    static_cast<uint64_t>(cnt_old) * 1000ULL /
                                        autoreload);
      case 1:
        return MicrosecondTimestamp(static_cast<uint64_t>(tick_new) * 1000ULL +
                                    static_cast<uint64_t>(cnt_new) * 1000ULL /
                                        autoreload);
      default:
        continue;
    }
  } while (true);
}

#endif
}  // namespace

STM32Timebase::STM32Timebase()
{
  ConfigureWrapRange(static_cast<uint64_t>(UINT32_MAX) * 1000ULL + 999ULL, UINT32_MAX);
  g_backend = STM32TimebaseBackend::SYSTICK;
  SetReady();
}

MicrosecondTimestamp Timebase::GetMicroseconds()
{
  switch (g_backend)
  {
    case STM32TimebaseBackend::SYSTICK:
      return GetSysTickMicroseconds();
#ifdef HAL_TIM_MODULE_ENABLED
    case STM32TimebaseBackend::TIMER:
      return GetTimerMicroseconds(STM32TimerTimebase::htim);
#endif
  }

  ASSERT(false);
  return MicrosecondTimestamp(0ULL);
}

MillisecondTimestamp Timebase::GetMilliseconds() { return HAL_GetTick(); }

#ifdef HAL_TIM_MODULE_ENABLED

TIM_HandleTypeDef* STM32TimerTimebase::htim = nullptr;

STM32TimerTimebase::STM32TimerTimebase(TIM_HandleTypeDef* timer)
{
  htim = timer;
  ConfigureWrapRange(static_cast<uint64_t>(UINT32_MAX) * 1000ULL + 999ULL, UINT32_MAX);
  g_backend = STM32TimebaseBackend::TIMER;
  SetReady();
}

#endif
