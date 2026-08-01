#include "thread.hpp"

#include "libxr_system.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(xTaskGetCurrentTaskHandle()); }

ThreadTimestamp Thread::GetTime()
{
  return ThreadTimestamp(static_cast<uint32_t>(xTaskGetTickCount()));
}

ThreadTimestamp Thread::GetTimeFromISR()
{
  return ThreadTimestamp(static_cast<uint32_t>(xTaskGetTickCountFromISR()));
}

void Thread::Sleep(uint32_t milliseconds)
{
  uint64_t remaining = Detail::MillisecondsToFreeRTOSTicks(milliseconds);
  do
  {
    const TickType_t delay = Detail::FreeRTOSDelayChunk(remaining);
    vTaskDelay(delay);
    remaining -= delay;
  } while (remaining != 0U);
}

void Thread::SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  ASSERT(time_to_sleep > 0U);

  TickType_t wake_time = static_cast<TickType_t>(last_wakeup_time.RawTicks());
  const uint64_t period_ticks = Detail::MillisecondsToFreeRTOSTicks(time_to_sleep);
  const uint64_t tick_range = static_cast<uint64_t>(portMAX_DELAY) + 1U;
  ASSERT(period_ticks < tick_range / 2U);
  const TickType_t period = static_cast<TickType_t>(period_ticks);
  vTaskDelayUntil(&wake_time, period);
  last_wakeup_time = ThreadTimestamp(static_cast<uint32_t>(wake_time));
}
