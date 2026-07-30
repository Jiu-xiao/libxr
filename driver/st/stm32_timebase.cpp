#include "stm32_timebase.hpp"

#include "stm32_timebase_internal.hpp"

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
    const uint32_t tick_before = HAL_GetTick();
    const uint32_t counter_before = SysTick->VAL;
    const bool exception_pending = (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U;
    const uint32_t counter_after = SysTick->VAL;
    const uint32_t tick_after = HAL_GetTick();
    const uint64_t period_counts = static_cast<uint64_t>(SysTick->LOAD) + 1U;

    const auto sample = STM32TimebaseInternal::ResolveSysTickSample(
        tick_before, counter_before, exception_pending, counter_after, tick_after,
        period_counts);
    if (sample.stable)
    {
      return MicrosecondTimestamp(sample.microseconds);
    }
  } while (true);
}

#ifdef HAL_TIM_MODULE_ENABLED
MicrosecondTimestamp GetTimerMicroseconds(TIM_HandleTypeDef* htim)
{
  ASSERT(htim != nullptr);

  do
  {
    const uint32_t tick_before = HAL_GetTick();
    const uint32_t counter_before = __HAL_TIM_GET_COUNTER(htim);
    const bool update_pending = __HAL_TIM_GET_FLAG(htim, TIM_FLAG_UPDATE) != 0U;
    const uint32_t counter_after = __HAL_TIM_GET_COUNTER(htim);
    const uint32_t tick_after = HAL_GetTick();
    const uint64_t period_counts =
        static_cast<uint64_t>(__HAL_TIM_GET_AUTORELOAD(htim)) + 1U;

    const auto sample = STM32TimebaseInternal::ResolveUpCounterSample(
        tick_before, counter_before, update_pending, counter_after, tick_after,
        period_counts);
    if (sample.stable)
    {
      return MicrosecondTimestamp(sample.microseconds);
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
