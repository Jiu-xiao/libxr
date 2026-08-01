#include "thread.hpp"

#include <sys/time.h>
#include <webots/robot.h>

#include "libxr_system.hpp"
#include "monotonic_time.hpp"

using namespace LibXR;

extern condition_var_handle* _libxr_webots_time_notify;

Thread Thread::Current(void) { return Thread(pthread_self()); }

ErrorCode Thread::Join()
{
  return pthread_join(thread_handle_, nullptr) == 0 ? ErrorCode::OK : ErrorCode::FAILED;
}

static ErrorCode ConditionVarWait(uint32_t timeout)
{
  const uint64_t deadline_ms = MonotonicTime::NowMilliseconds() + timeout;

  while (MonotonicTime::RemainingMilliseconds(deadline_ms) > 0)
  {
    pthread_mutex_lock(&_libxr_webots_time_notify->mutex);
    WebotsMarkCurrentRealtimeThreadParked(true);
    const timespec ts =
        MonotonicTime::RealtimeDeadlineFromNow(MonotonicTime::WaitSliceMilliseconds(
            MonotonicTime::RemainingMilliseconds(deadline_ms)));
    auto ans = pthread_cond_timedwait(&_libxr_webots_time_notify->cond,
                                      &_libxr_webots_time_notify->mutex, &ts);
    WebotsMarkCurrentRealtimeThreadRunning();
    pthread_mutex_unlock(&_libxr_webots_time_notify->mutex);
    if (ans == 0)
    {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::TIMEOUT;
}

void Thread::Sleep(uint32_t milliseconds)
{
  const uint64_t deadline_ms = MonotonicTime::NowMilliseconds() + milliseconds;
  while (MonotonicTime::RemainingMilliseconds(deadline_ms) > 0)
  {
    ConditionVarWait(MonotonicTime::WaitSliceMilliseconds(
        MonotonicTime::RemainingMilliseconds(deadline_ms)));
  }
}

void Thread::SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  constexpr uint32_t HALF_RANGE = UINT32_C(1) << 31U;
  ASSERT(time_to_sleep > 0U && time_to_sleep < HALF_RANGE);

  last_wakeup_time = ThreadTimestamp(last_wakeup_time.RawTicks() + time_to_sleep);

  while (true)
  {
    const uint32_t remaining =
        Detail::SchedulerTicksUntil(GetTime().RawTicks(), last_wakeup_time.RawTicks());
    if (remaining == 0U)
    {
      return;
    }
    ConditionVarWait(MonotonicTime::WaitSliceMilliseconds(remaining));
  }
}

ThreadTimestamp Thread::GetTime()
{
  return ThreadTimestamp(static_cast<uint32_t>(MonotonicTime::NowMilliseconds()));
}

ThreadTimestamp Thread::GetTimeFromISR() { return GetTime(); }

void Thread::Yield() { sched_yield(); }
