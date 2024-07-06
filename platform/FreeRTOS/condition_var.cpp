#include "condition_var.hpp"

#include <time.h>

#include "libxr_def.hpp"

using namespace LibXR;

ConditionVar::ConditionVar()
    : handle_(xSemaphoreCreateCounting(UINT32_MAX, 0)) {}

ConditionVar::~ConditionVar() { vSemaphoreDelete(handle_); }

ErrorCode ConditionVar::Wait(uint32_t timeout) {
  if (xSemaphoreTake(handle_, timeout) == pdTRUE) {
    return ErrorCode::OK;
  } else {
    return ErrorCode::TIMEOUT;
  }
}

void ConditionVar::Signal() { xSemaphoreGive(handle_); }

void ConditionVar::Broadcast() {
  while (xSemaphoreTake(handle_, 0) != pdTRUE) {
    xSemaphoreGive(handle_);
  }

  xSemaphoreTake(handle_, 0);
}
