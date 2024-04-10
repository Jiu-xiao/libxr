#pragma once

#include "thread.hpp"
#include "libxr_platform.hpp"
#include <errno.h>
#include <sys/time.h>

using namespace LibXR;

extern struct timeval _libxr_linux_start_time;

template <typename ArgType>
Thread Thread::Create(ArgType arg, void (*function)(ArgType), const char *name,
                      size_t stack_depth, Priority priority) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (stack_depth > 256) {
    pthread_attr_setstacksize(&attr, stack_depth / 1024 * 4);
  } else {
    pthread_attr_setstacksize(&attr, 1);
  }

  class ThreadBlock {
  public:
    ThreadBlock(typeof(function) fun, ArgType arg, const char *name)
        : fun_(fun), arg_(arg),
          name_(reinterpret_cast<char *>(malloc(strlen(name) + 1))) {
      strcpy(name_, name);
    }

    static void *Port(void *arg) {
      ThreadBlock *block = static_cast<ThreadBlock *>(arg);
      const char *thread_name = block->name_;
      block->fun_(block->arg_);
    }

    char *name_;
    typeof(function) fun_;
    ArgType arg_;
  };

  auto block = new ThreadBlock(function, arg, name);

  pthread_create(&this->thread_handle_, &attr, block->Port, block);

  if (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR) >=
      PRIORITY_REALTIME) {
    struct sched_param sp;
    bzero((void *)&sp, sizeof(sp));
    sp.sched_priority = sched_get_priority_min(SCHED_RR) + priority;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
  }
}

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
