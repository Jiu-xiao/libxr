#include "thread.hpp"

#include <sys/time.h>

#include <cerrno>

#include "libxr_def.hpp"

using namespace LibXR;

extern struct timeval libxr_linux_start_time;
extern struct timespec libxr_linux_start_time_spec;

Thread Thread::Current(void) { return Thread(pthread_self()); }

void Thread::Sleep(uint32_t milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = static_cast<__syscall_slong_t>((milliseconds % 1000) * 1000000);
  UNUSED(clock_nanosleep(CLOCK_REALTIME, 0, &ts, nullptr));
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  last_waskup_time = last_waskup_time + time_to_sleep;

  struct timespec ts = libxr_linux_start_time_spec;
  uint32_t add = 0;
  uint32_t secs = last_waskup_time / 1000;
  int64_t raw_time = static_cast<int64_t>(last_waskup_time) * 1000U * 1000U;
  add = raw_time / (static_cast<int64_t>(1000U * 1000U * 1000U));
  ts.tv_sec += (add + secs);
  ts.tv_nsec = raw_time % (static_cast<int64_t>(1000U * 1000U * 1000U));

  while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, &ts) &&
         errno == EINTR) {
  }
}

uint32_t Thread::GetTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000 +
          (tv.tv_usec - libxr_linux_start_time.tv_usec) / 1000) %
         UINT32_MAX;
}

void Thread::Yield() { sched_yield(); }
