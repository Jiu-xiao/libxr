#include "mutex.hpp"

#include <pthread.h>

#include "libxr_platform.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(xSemaphoreCreateMutex()) {}

Mutex::~Mutex() { vSemaphoreDelete(mutex_handle_); }

ErrorCode Mutex::Lock() {
  if (xSemaphoreTake(mutex_handle_, UINT32_MAX)) {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

ErrorCode Mutex::TryLock() {
  if (xSemaphoreTake(mutex_handle_, 0)) {
    return ErrorCode::BUSY;
  }
  return ErrorCode::OK;
}

void Mutex::Unlock() { xSemaphoreGive(mutex_handle_); }
