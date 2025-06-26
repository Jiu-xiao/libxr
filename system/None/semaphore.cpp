#include "semaphore.hpp"

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "timer.hpp"

using namespace LibXR;

Semaphore::Semaphore(uint32_t init_count) : semaphore_handle_(init_count) {}

Semaphore::~Semaphore() {}

void Semaphore::Post() { semaphore_handle_++; }

ErrorCode Semaphore::Wait(uint32_t timeout) {
  if (semaphore_handle_ > 0) {
    semaphore_handle_--;
    return ErrorCode::OK;
  } else if (timeout == 0) {
    return ErrorCode::TIMEOUT;
  }

  uint32_t now = Timebase::GetMilliseconds();

  while (uint32_t(Timebase::GetMilliseconds()) - now < timeout) {
    if (semaphore_handle_ > 0) {
      semaphore_handle_--;
      return ErrorCode::OK;
    }
    Timer::RefreshTimerInIdle();
  }
  return ErrorCode::TIMEOUT;
}

void Semaphore::PostFromCallback(bool in_isr) {
  UNUSED(in_isr);
  Post();
}

size_t Semaphore::Value() { return semaphore_handle_; }
