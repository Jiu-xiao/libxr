#include "thread.hpp"

#include "timebase.hpp"
#include "timer.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(); }

void Thread::Sleep(uint32_t milliseconds)
{
  uint32_t now = Timebase::GetMilliseconds();
  while (uint32_t(Timebase::GetMilliseconds()) - now < milliseconds)
  {
    Timer::RefreshTimerInIdle();
  }
}

void Thread::SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  constexpr uint32_t HALF_RANGE = UINT32_C(1) << 31U;
  ASSERT(time_to_sleep > 0U && time_to_sleep < HALF_RANGE);

  while (uint32_t(Timebase::GetMilliseconds()) - last_wakeup_time.RawTicks() <
         time_to_sleep)
  {
    Timer::RefreshTimerInIdle();
  }
  last_wakeup_time = ThreadTimestamp(last_wakeup_time.RawTicks() + time_to_sleep);
}

ThreadTimestamp Thread::GetTime()
{
  return ThreadTimestamp(static_cast<uint32_t>(Timebase::GetMilliseconds()));
}

ThreadTimestamp Thread::GetTimeFromISR() { return GetTime(); }

void Thread::Yield() {}
