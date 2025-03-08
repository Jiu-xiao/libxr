#pragma once

#include <atomic>

namespace LibXR {
class SpinLock {
 private:
  std::atomic_flag atomic_flag_ = ATOMIC_FLAG_INIT;

 public:
  bool TryLock() noexcept {
    return !atomic_flag_.test_and_set(std::memory_order_acquire);
  }

  void Lock() noexcept {
    while (atomic_flag_.test_and_set(std::memory_order_acquire)) {
    }
  }

  void Unlock() noexcept { atomic_flag_.clear(std::memory_order_release); }
};
}  // namespace LibXR
