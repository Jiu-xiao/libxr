#include "thread.hpp"

#include <sys/time.h>
#include <webots/robot.h>

#include "libxr_system.hpp"

using namespace LibXR;

extern condition_var_handle *_libxr_webots_time_notify;

Thread Thread::Current(void) { return Thread(pthread_self()); }

static void ConditionVarWait(uint32_t timeout)
{
  uint32_t start_time = _libxr_webots_time_count;

  struct timespec ts;
  UNUSED(clock_gettime(CLOCK_REALTIME, &ts));

  uint32_t add = 0;
  int64_t raw_time = static_cast<__syscall_slong_t>(1U * 1000U * 1000U) + ts.tv_nsec;
  add = raw_time / (static_cast<int64_t>(1000U * 1000U * 1000U));

  ts.tv_sec += add;
  ts.tv_nsec = raw_time % (static_cast<int64_t>(1000U * 1000U * 1000U));

  while (_libxr_webots_time_count - start_time < timeout)
  {
    pthread_mutex_lock(_libxr_webots_time_notify->mutex);
    auto ans = pthread_cond_timedwait(_libxr_webots_time_notify->cond,
                                      _libxr_webots_time_notify->mutex, &ts);
    pthread_mutex_unlock(_libxr_webots_time_notify->mutex);
    if (ans == 0)
    {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::TIMEOUT;
}

void Thread::Sleep(uint32_t milliseconds)
{
  uint32_t now = _libxr_webots_time_count;
  while (_libxr_webots_time_count - now < milliseconds)
  {
    ConditionVarWait(1);
  }
}

void Thread::SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep)
{
  last_waskup_time = last_waskup_time + time_to_sleep;

  while (_libxr_webots_time_count < last_waskup_time)
  {
    ConditionVarWait(1);
  }
}

uint32_t Thread::GetTime() { return _libxr_webots_time_count; }

void Thread::Yield() { sched_yield(); }
