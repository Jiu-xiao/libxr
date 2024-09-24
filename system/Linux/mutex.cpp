#include "mutex.hpp"
#include "libxr_system.hpp"
#include <pthread.h>

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
