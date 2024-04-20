#include "libxr_def.hpp"
#include <atomic>

namespace LibXR {
class SpinLock {
private:
  std::atomic_flag atomic_flag = ATOMIC_FLAG_INIT;

public:
  bool TryLock() noexcept {
    return !atomic_flag.test_and_set(std::memory_order_acquire);
  }

  void Lock() noexcept {
    while (atomic_flag.test_and_set(std::memory_order_acquire)) {
    }
  }

  void UnLock() noexcept { atomic_flag.clear(std::memory_order_release); }
};
} // namespace LibXR
