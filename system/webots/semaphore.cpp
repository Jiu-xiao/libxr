#include "semaphore.hpp"

#include <semaphore.h>

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
  if (!sem_trywait(this->semaphore_handle_))
  {
    return ErrorCode::OK;
  }

  if (!timeout)
  {
    return ErrorCode::TIMEOUT;
  }

  const uint64_t deadline_ms = MonotonicTime::NowMilliseconds() + timeout;

  while (MonotonicTime::RemainingMilliseconds(deadline_ms) > 0)
  {
    const timespec ts =
        MonotonicTime::RealtimeDeadlineFromNow(MonotonicTime::WaitSliceMilliseconds(
            MonotonicTime::RemainingMilliseconds(deadline_ms)));

    if (!sem_timedwait(semaphore_handle_, &ts))
    {
      return ErrorCode::OK;
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
