#pragma once

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"

namespace LibXR {
class Semaphore {
public:
  Semaphore(uint32_t init_count = 0);

  ~Semaphore();

  void Post();

  void PostFromCallback(bool in_isr);

  ErrorCode Wait(uint32_t timeout = UINT32_MAX);

  size_t Value();

private:
  libxr_semaphore_handle semaphore_handle_;
};
} // namespace LibXR