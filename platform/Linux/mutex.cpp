#include "mutex.hpp"
#include "libxr_platform.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(PTHREAD_MUTEX_INITIALIZER) {}

ErrorCode Mutex::Lock() {
  if (pthread_mutex_lock(&mutex_handle_) != 0) {
    return ERR_BUSY;
  }
  return ErrorCode::NO_ERR;
}

ErrorCode Mutex::TryLock() {
  if (pthread_mutex_trylock(&mutex_handle_) != 0) {
    return ErrorCode::ERR_BUSY;
  }
  return ErrorCode::NO_ERR;
}

void Mutex::UnLock() { pthread_mutex_unlock(&mutex_handle_); }
