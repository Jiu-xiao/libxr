#include "thread.hpp"

#include "timer.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(); }

void Thread::Sleep(uint32_t milliseconds) {
  uint32_t now = Timebase::GetMilliseconds();
  while (Timebase::GetMilliseconds() - now < milliseconds) {
    Timer::RefreshTimerInIdle();
  }
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  while (Timebase::GetMilliseconds() - last_waskup_time < time_to_sleep) {
    Timer::RefreshTimerInIdle();
  }
  last_waskup_time = last_waskup_time + time_to_sleep;
}

uint32_t Thread::GetTime() { return Timebase::GetMilliseconds(); }

void Thread::Yield() {}
