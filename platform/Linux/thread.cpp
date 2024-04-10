#include "thread.hpp"
#include "libxr_platform.hpp"
#include <errno.h>
#include <sys/time.h>

using namespace LibXR;

extern struct timeval _libxr_linux_start_time;

Thread Thread::Current(void) { return Thread(pthread_self()); }

void Thread::Sleep(uint32_t milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
}

void Thread::SleepUntil(TimestampMS millisecond) {
  struct timespec ts;
  uint32_t add = 0;
  uint32_t secs = millisecond / 1000;
  long raw_time = millisecond * 1000U * 1000U + ts.tv_nsec;
  add = raw_time / (1000U * 1000U * 1000U);
  ts.tv_sec += (add + secs);
  ts.tv_nsec = raw_time % (1000U * 1000U * 1000U);

  while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, &ts) &&
         errno == EINTR)
    ;
}

uint32_t Thread::GetTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((tv.tv_sec - _libxr_linux_start_time.tv_sec) * 1000 +
          (tv.tv_usec - _libxr_linux_start_time.tv_usec) / 1000) %
         UINT32_MAX;
}

void Thread::Yield() { sched_yield(); }
