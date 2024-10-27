#include "condition_var.hpp"

#include <time.h>

#include "libxr_def.hpp"

using namespace LibXR;

ConditionVar::ConditionVar() {
  pthread_mutex_init(&handle_.mutex, nullptr);
  pthread_cond_init(&handle_.cond, nullptr);
}

ConditionVar::~ConditionVar() {
  pthread_mutex_destroy(&handle_.mutex);
  pthread_cond_destroy(&handle_.cond);
}

ErrorCode ConditionVar::Wait(uint32_t timeout) {
  uint32_t start_time = _libxr_webots_time_count;

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  uint32_t add = 0;
  long raw_time = 1U * 1000U * 1000U + ts.tv_nsec;
  add = raw_time / (1000U * 1000U * 1000U);

  ts.tv_sec += add;
  ts.tv_nsec = raw_time % (1000U * 1000U * 1000U);

  while (_libxr_webots_time_count - start_time < timeout) {
    pthread_mutex_lock(&handle_.mutex);
    auto ans = pthread_cond_timedwait(&handle_.cond, &handle_.mutex, &ts);
    pthread_mutex_unlock(&handle_.mutex);
    if (ans == 0) {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::TIMEOUT;
}

void ConditionVar::Signal() {
  pthread_mutex_lock(&handle_.mutex);
  pthread_cond_signal(&handle_.cond);
  pthread_mutex_unlock(&handle_.mutex);
}

void ConditionVar::Broadcast() {
  pthread_mutex_lock(&handle_.mutex);
  pthread_cond_broadcast(&handle_.cond);
  pthread_mutex_unlock(&handle_.mutex);
}
