#include "thread.hpp"

#include "timebase.hpp"
#include "tx_api.h"

using namespace LibXR;

void Thread::Sleep(uint32_t milliseconds)
{
  tx_thread_sleep(Detail::MillisecondsToThreadXTicks(milliseconds));
}

void Thread::SleepUntil(MillisecondTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  const uint32_t target = static_cast<uint32_t>(last_wakeup_time) + time_to_sleep;
  const uint32_t now = Timebase::GetMilliseconds();
  const uint32_t remaining = target - now;
  if (remaining != 0U && remaining < (UINT32_C(1) << 31U))
  {
    Sleep(remaining);
  }
  last_wakeup_time = target;
}

uint32_t Thread::GetTime() { return Timebase::GetMilliseconds(); }

void Thread::Yield() { tx_thread_relinquish(); }
