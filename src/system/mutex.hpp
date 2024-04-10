#pragma once

#include "libxr_def.hpp"
#include "libxr_platform.hpp"

namespace LibXR {
class Mutex {
public:
  Mutex();
  ErrorCode Lock();
  ErrorCode TryLock();
  void UnLock();

private:
  libxr_mutex_handle mutex_handle_;
};

} // namespace LibXR