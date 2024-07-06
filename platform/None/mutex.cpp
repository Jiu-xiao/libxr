#include "mutex.hpp"
#include "libxr_platform.hpp"
#include <pthread.h>

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(0) {}

Mutex::~Mutex() {}

ErrorCode Mutex::Lock() {
  while (mutex_handle_ < 1) {
  }
  mutex_handle_ = 0;
  return ErrorCode::OK;
}

ErrorCode Mutex::TryLock() {
  if (mutex_handle_ == 0) {
    return ErrorCode::BUSY;
  }
  mutex_handle_ = 0;
  return ErrorCode::OK;
}

void Mutex::Unlock() { mutex_handle_ = 1; }
