#include "semaphore.hpp"

#include <semaphore.h>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

using namespace LibXR;

Semaphore::Semaphore(uint32_t init_count) {
  sem_init(&semaphore_handle_, 0, init_count);
}

Semaphore::~Semaphore() { sem_destroy(&semaphore_handle_); }

void Semaphore::Post() { sem_post(&semaphore_handle_); }

ErrorCode Semaphore::Wait(uint32_t timeout) {
  if (!sem_trywait(&this->semaphore_handle_)) {
    return ErrorCode::OK;
  }

  if (!timeout) {
    return ErrorCode::TIMEOUT;
  }

  uint32_t start_time = _libxr_webots_time_count;
  bool ans = false;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  uint32_t add = 0;
  long raw_time = 1U * 1000U * 1000U + ts.tv_nsec;
  add = raw_time / (1000U * 1000U * 1000U);

  ts.tv_sec += add;
  ts.tv_nsec = raw_time % (1000U * 1000U * 1000U);

  while (_libxr_webots_time_count - start_time < timeout) {
    ans = !sem_timedwait(&semaphore_handle_, &ts);
    if (ans) {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::TIMEOUT;
}

void Semaphore::PostFromCallback(bool in_isr) {
  UNUSED(in_isr);
  Post();
}

size_t Semaphore::Value() {
  int value = 0;
  sem_getvalue(&semaphore_handle_, &value);
  return value;
}
