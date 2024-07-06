#include "thread.hpp"

#include "timer.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(); }

void Thread::Sleep(uint32_t milliseconds) {
  uint32_t now = libxr_get_time_ms();
  while (libxr_get_time_ms() - now < milliseconds) {
    Timer::RefreshTimerInIdle();
  }
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  while (libxr_get_time_ms() - last_waskup_time < time_to_sleep) {
    Timer::RefreshTimerInIdle();
  }
  last_waskup_time = last_waskup_time + time_to_sleep;
}

uint32_t Thread::GetTime() { return libxr_get_time_ms(); }

void Thread::Yield() {}
