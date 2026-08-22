#include "thread.hpp"

#include "tx_api.h"

using namespace LibXR;

void Thread::Sleep(uint32_t milliseconds)
{
  tx_thread_sleep(Detail::MillisecondsToThreadXTicks(milliseconds));
}

void Thread::SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  ASSERT(time_to_sleep > 0U);

  const uint32_t period =
      static_cast<uint32_t>(Detail::MillisecondsToThreadXTicks(time_to_sleep));
  constexpr uint32_t HALF_RANGE = UINT32_C(1) << 31U;
  ASSERT(period < HALF_RANGE);

  const uint32_t target = last_wakeup_time.RawTicks() + period;
  const uint32_t remaining =
      Detail::SchedulerTicksUntil(static_cast<uint32_t>(tx_time_get()), target);
  if (remaining != 0U)
  {
    tx_thread_sleep(static_cast<ULONG>(remaining));
  }
  last_wakeup_time = ThreadTimestamp(target);
}

ThreadTimestamp Thread::GetTime()
{
  return ThreadTimestamp(static_cast<uint32_t>(tx_time_get()));
}

ThreadTimestamp Thread::GetTimeFromISR() { return GetTime(); }

void Thread::Yield() { tx_thread_relinquish(); }
