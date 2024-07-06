#include "semaphore.hpp"

#include "libxr_def.hpp"
#include "libxr_platform.hpp"

using namespace LibXR;

Semaphore::Semaphore(uint32_t init_count)
    : semaphore_handle_(xSemaphoreCreateCounting(UINT32_MAX, init_count)) {}

Semaphore::~Semaphore() { vSemaphoreDelete(semaphore_handle_); }

void Semaphore::Post() { xSemaphoreGive(semaphore_handle_); }

ErrorCode Semaphore::Wait(uint32_t timeout) {
  if (xSemaphoreTake(semaphore_handle_, timeout) == pdTRUE) {
    return ErrorCode::OK;
  } else {
    return ErrorCode::TIMEOUT;
  }
}

void Semaphore::PostFromCallback(bool in_isr) {
  if (in_isr) {
    BaseType_t px_higher_priority_task_woken = 0;
    xSemaphoreGiveFromISR(semaphore_handle_, &px_higher_priority_task_woken);
    if (px_higher_priority_task_woken != pdFALSE) {
      portYIELD();
    }
  } else {
    Post();
  }
}

size_t Semaphore::Value() {
  uint32_t value = uxSemaphoreGetCount(&semaphore_handle_);
  return value;
}
