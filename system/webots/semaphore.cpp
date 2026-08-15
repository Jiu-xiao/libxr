#include "semaphore.hpp"

#include <semaphore.h>

#include <cerrno>
#include <cstddef>

#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "monotonic_time.hpp"

using namespace LibXR;

Semaphore::Semaphore(uint32_t init_count) : semaphore_handle_(new sem_t)
{
  sem_init(semaphore_handle_, 0, init_count);
}

Semaphore::~Semaphore()
{
  sem_destroy(semaphore_handle_);
  delete semaphore_handle_;
}

void Semaphore::Post() { sem_post(semaphore_handle_); }

ErrorCode Semaphore::Wait(uint32_t timeout)
{
  while (true)
  {
    if (sem_trywait(semaphore_handle_) == 0)
    {
      return ErrorCode::OK;
    }
    if (errno == EINTR)
    {
      continue;
    }
    if (errno != EAGAIN)
    {
      return ErrorCode::FAILED;
    }
    break;
  }

  if (!timeout)
  {
    return ErrorCode::TIMEOUT;
  }

  if (timeout == UINT32_MAX)
  {
    WebotsMarkCurrentRealtimeThreadParked(false);
    int wait_ans = 0;
    do
    {
      wait_ans = sem_wait(semaphore_handle_);
    } while (wait_ans != 0 && errno == EINTR);
    WebotsMarkCurrentRealtimeThreadRunning();
    return wait_ans == 0 ? ErrorCode::OK : ErrorCode::FAILED;
  }

  const uint64_t deadline_ms = MonotonicTime::NowMilliseconds() + timeout;

  while (MonotonicTime::RemainingMilliseconds(deadline_ms) > 0)
  {
    const timespec ts =
        MonotonicTime::RealtimeDeadlineFromNow(MonotonicTime::WaitSliceMilliseconds(
            MonotonicTime::RemainingMilliseconds(deadline_ms)));

    WebotsMarkCurrentRealtimeThreadParked(false);
    const int wait_ans = sem_timedwait(semaphore_handle_, &ts);
    WebotsMarkCurrentRealtimeThreadRunning();
    if (!wait_ans)
    {
      return ErrorCode::OK;
    }
    if (errno != EINTR && errno != ETIMEDOUT)
    {
      return ErrorCode::FAILED;
    }
  }

  return ErrorCode::TIMEOUT;
}

void Semaphore::PostFromCallback(bool in_isr)
{
  UNUSED(in_isr);
  Post();
}

size_t Semaphore::Value()
{
  int value = 0;
  sem_getvalue(semaphore_handle_, &value);
  return value;
}
