#include "mutex.hpp"

#include <pthread.h>

#include "libxr_system.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(xSemaphoreCreateBinary()) { Unlock(); }

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

ErrorCode Mutex::TryLockInCallback(bool in_isr) {
  if (in_isr) {
    BaseType_t px_higher_priority_task_woken = 0;
    BaseType_t ans =
        xSemaphoreTakeFromISR(mutex_handle_, &px_higher_priority_task_woken);

    if (ans == pdPASS) {
      if (px_higher_priority_task_woken != pdFALSE) {
        portYIELD();  // NOLINT
      }

      return ErrorCode::OK;
    } else {
      return ErrorCode::BUSY;
    }
  } else {
    return TryLock();
  }
}

void Mutex::UnlockInCallback(bool in_isr) {
  if (in_isr) {
    BaseType_t px_higher_priority_task_woken = 0;
    xSemaphoreGiveFromISR(mutex_handle_, &px_higher_priority_task_woken);
    if (px_higher_priority_task_woken != pdFALSE) {
      portYIELD();  // NOLINT
    }
  } else {
    Unlock();
  }
}
