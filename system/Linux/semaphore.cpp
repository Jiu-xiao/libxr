#include "semaphore.hpp"

#include <semaphore.h>

#include <cstddef>
#include <iostream>

#include "libxr_def.hpp"

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
  struct timespec ts;
  UNUSED(clock_gettime(CLOCK_REALTIME, &ts));
  uint32_t secs = timeout / 1000;
  timeout = timeout % 1000;

  uint32_t add = 0;
  int64_t raw_time = static_cast<__syscall_slong_t>(timeout * 1000U * 1000U) + ts.tv_nsec;
  add = raw_time / (static_cast<int64_t>(1000U * 1000U * 1000U));
  ts.tv_sec += (add + secs);
  ts.tv_nsec = raw_time % (static_cast<int64_t>(1000U * 1000U * 1000U));

  if (sem_timedwait(semaphore_handle_, &ts) == 0)
  {
    return ErrorCode::OK;
  }
  else
  {
    return ErrorCode::TIMEOUT;
  }
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
