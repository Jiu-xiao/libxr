#pragma once

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
  ErrorCode TryLockInCallback(bool in_isr);
  void UnlockFromCallback(bool in_isr);

  class LockGuardInCallback;

  class LockGuard {
   public:
    LockGuard(Mutex &mutex) : mutex_(mutex) { mutex_.Lock(); }
    ~LockGuard() { mutex_.Unlock(); }

   private:
    Mutex &mutex_;
  };

  class LockGuardInCallback {
   public:
    LockGuardInCallback(Mutex &mutex, bool in_isr)
        : mutex_(mutex),
          success_(mutex_.TryLockInCallback(in_isr)),
          in_isr_(in_isr) {}

    ~LockGuardInCallback() {
      if (success_ == ErrorCode::OK) {
        mutex_.UnlockFromCallback(in_isr_);
      }
    }

    bool Locked() { return success_ == ErrorCode::OK; }

   private:
    Mutex &mutex_;
    ErrorCode success_;
    bool in_isr_;
  };

 private:
  libxr_mutex_handle mutex_handle_;
};

}  // namespace LibXR
