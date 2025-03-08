#include "thread.hpp"

#include <sys/time.h>
#include <webots/robot.h>

#include "condition_var.hpp"
#include "libxr_system.hpp"

using namespace LibXR;

extern LibXR::ConditionVar *_libxr_webots_time_notify;

Thread Thread::Current(void) { return Thread(pthread_self()); }

void Thread::Sleep(uint32_t milliseconds) {
  uint32_t now = _libxr_webots_time_count;
  while (_libxr_webots_time_count - now < milliseconds) {
    _libxr_webots_time_notify->Wait(1);
  }
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep) {
  last_waskup_time = last_waskup_time + time_to_sleep;

  while (_libxr_webots_time_count < last_waskup_time) {
    _libxr_webots_time_notify->Wait(1);
  }
}

uint32_t Thread::GetTime() { return _libxr_webots_time_count; }

void Thread::Yield() { sched_yield(); }
