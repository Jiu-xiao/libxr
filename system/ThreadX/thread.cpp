#include "thread.hpp"

#include "timebase.hpp"
#include "timer.hpp"
#include "tx_api.h"

using namespace LibXR;

void Thread::Sleep(uint32_t milliseconds)
{
  tx_thread_sleep(milliseconds * TX_TIMER_TICKS_PER_SECOND / 1000);
}

void Thread::SleepUntil(MillisecondTimestamp& last_waskup_time, uint32_t time_to_sleep)
{
  uint32_t target = last_waskup_time + time_to_sleep;
  uint32_t now = Timebase::GetMilliseconds();
  if (target > now)
  {
    tx_thread_sleep((target - now) * TX_TIMER_TICKS_PER_SECOND / 1000);
  }
  last_waskup_time = target;
}

uint32_t Thread::GetTime() { return Timebase::GetMilliseconds(); }

void Thread::Yield() { tx_thread_relinquish(); }
