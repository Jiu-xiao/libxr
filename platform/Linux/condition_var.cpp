#include "condition_var.hpp"
#include "libxr_def.hpp"
#include <time.h>

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
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout / 1000;
  ts.tv_nsec += (timeout % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }

  pthread_mutex_lock(&handle_.mutex);
  auto ans = pthread_cond_timedwait(&handle_.cond, &handle_.mutex, &ts);
  pthread_mutex_unlock(&handle_.mutex);

  if (ans == 0) {
    return ErrorCode::OK;
  } else {
    return ErrorCode::TIMEOUT;
  }
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
