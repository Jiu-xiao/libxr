#include "condition_var.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include <cstdint>
#include <time.h>

using namespace LibXR;

ConditionVar::ConditionVar() { handle_ = 0; }

ConditionVar::~ConditionVar() {}

ErrorCode ConditionVar::Wait(uint32_t timeout) {
  if (handle_) {
    handle_ = 0;
    return ErrorCode::OK;
  } else if (timeout == 0) {
    return ErrorCode::TIMEOUT;
  }

  uint32_t now = libxr_get_time_ms();

  while (libxr_get_time_ms() - now < timeout) {
    if (handle_) {
      return ErrorCode::OK;
    }
  }

  return ErrorCode::TIMEOUT;
}

void ConditionVar::Signal() {
  handle_ = 1;
}

void ConditionVar::Broadcast() {
  handle_ = 1;
}
