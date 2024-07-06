#include "thread.hpp"
#include "libxr_platform.hpp"
#include <cstdint>
#include <errno.h>
#include <sys/time.h>

using namespace LibXR;

extern struct timeval _libxr_linux_start_time;
extern struct timespec _libxr_linux_start_time_spec;

Thread Thread::Current(void) { return Thread(); }

void Thread::Sleep(uint32_t milliseconds) {
  uint32_t now = libxr_get_time_ms();
  while (libxr_get_time_ms() - now < milliseconds) {
  }
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  while (libxr_get_time_ms() - last_waskup_time < time_to_sleep) {
  }
  last_waskup_time = last_waskup_time + time_to_sleep;
}

uint32_t Thread::GetTime() { return libxr_get_time_ms(); }

void Thread::Yield() {}
