#include "mutex.hpp"

#include "libxr_system.hpp"
#include "thread.hpp"
#include "timer.hpp"

using namespace LibXR;

Mutex::Mutex() : mutex_handle_(1) {}

Mutex::~Mutex() {}

ErrorCode Mutex::Lock() {
  while (mutex_handle_ < 1) {
    Timer::RefreshTimerInIdle();
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

ErrorCode Mutex::TryLockInCallback(bool in_isr) {
  UNUSED(in_isr);
  return TryLock();
}

void Mutex::UnlockFromCallback(bool in_isr) {
  UNUSED(in_isr);
  Unlock();
}
