#pragma once

#include "libxr_def.hpp"
#include "thread.hpp"

namespace LibXR {
class Signal {
 public:
  static ErrorCode Action(Thread &thread, int signal);
  static ErrorCode ActionFromCallback(Thread &thread, int signal, bool in_isr);
  static ErrorCode Wait(int signal, uint32_t timeout = UINT32_MAX);
};
}  // namespace LibXR
