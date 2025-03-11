#include "mutex.hpp"

#include <pthread.h>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(PTHREAD_MUTEX_INITIALIZER) {}

Mutex::~Mutex() { pthread_mutex_destroy(&mutex_handle_); }

ErrorCode Mutex::Lock() {
  if (pthread_mutex_lock(&mutex_handle_) != 0) {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

ErrorCode Mutex::TryLock() {
  if (pthread_mutex_trylock(&mutex_handle_) != 0) {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

void Mutex::Unlock() { pthread_mutex_unlock(&mutex_handle_); }

ErrorCode Mutex::TryLockInCallback(bool in_isr) {
  UNUSED(in_isr);
  return TryLock();
}

void Mutex::UnlockFromCallback(bool in_isr) {
  UNUSED(in_isr);
  Unlock();
}
