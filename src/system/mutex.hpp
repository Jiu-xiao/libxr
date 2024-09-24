#pragma once

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR {
class Mutex {
public:
  Mutex();
  ~Mutex();

  ErrorCode Lock();
  ErrorCode TryLock();
  void Unlock();

private:
  libxr_mutex_handle mutex_handle_;
};

} // namespace LibXR