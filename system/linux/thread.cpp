#include "thread.hpp"

#include <cerrno>

#include "libxr_def.hpp"
#include "monotonic_time.hpp"

using namespace LibXR;

extern struct timespec libxr_linux_start_time_spec;

Thread Thread::Current(void) { return Thread(pthread_self()); }

void Thread::Sleep(uint32_t milliseconds)
{
  timespec ts = MonotonicTime::RelativeFromMilliseconds(milliseconds);
  while (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR)
  {
  }
}

void Thread::SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep)
{
  constexpr uint32_t HALF_RANGE = UINT32_C(1) << 31U;
  ASSERT(time_to_sleep > 0U && time_to_sleep < HALF_RANGE);

  last_wakeup_time = ThreadTimestamp(last_wakeup_time.RawTicks() + time_to_sleep);

  const uint64_t elapsed_ms =
      MonotonicTime::SharedToXrMicroseconds(MonotonicTime::NowMicroseconds()) / 1000U;
  const uint32_t remaining = Detail::SchedulerTicksUntil(
      static_cast<uint32_t>(elapsed_ms), last_wakeup_time.RawTicks());
  if (remaining == 0U)
  {
    return;
  }

  const timespec ts =
      MonotonicTime::AddMilliseconds(libxr_linux_start_time_spec, elapsed_ms + remaining);

  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
  {
  }
}

ThreadTimestamp Thread::GetTime()
{
  const uint64_t elapsed_us =
      MonotonicTime::SharedToXrMicroseconds(MonotonicTime::NowMicroseconds());
  return ThreadTimestamp(static_cast<uint32_t>(elapsed_us / 1000U));
}

ThreadTimestamp Thread::GetTimeFromISR() { return GetTime(); }

void Thread::Yield() { sched_yield(); }

ErrorCode Thread::Join()
{
  return pthread_join(thread_handle_, nullptr) == 0 ? ErrorCode::OK : ErrorCode::FAILED;
}
