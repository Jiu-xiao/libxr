#pragma once

#include <atomic>

namespace LibXR
{

/**
 * @class SpinLock
 * @brief 轻量级自旋锁实现 / Lightweight spinlock implementation
 *
 * 该类实现了自旋锁 (`SpinLock`)，适用于短时间的锁定操作，避免线程切换的开销。
 * This class implements a spinlock, which is suitable for short-term locking operations
 * to avoid the overhead of thread context switching.
 */
class SpinLock
{
 private:
  std::atomic_flag atomic_flag_ = ATOMIC_FLAG_INIT;  ///< 自旋锁标志 / Spinlock flag

 public:
  /**
   * @brief 尝试获取锁 / Attempts to acquire the lock
   * @return 如果获取成功返回 `true`，否则返回 `false` /
   *         Returns `true` if the lock is acquired successfully, otherwise returns
   * `false`
   *
   * 该方法不会阻塞当前线程，而是立即尝试获取锁。如果锁已被占用，则返回 `false`。
   * This method does not block the current thread but instead attempts to acquire the
   * lock immediately. If the lock is already held, it returns `false`.
   */
  bool TryLock() noexcept
  {
    return !atomic_flag_.test_and_set(std::memory_order_acquire);
  }

  /**
   * @brief 阻塞直到获取锁 / Blocks until the lock is acquired
   *
   * 该方法会不断轮询，直到成功获取锁，适用于临界区很短的场景。
   * This method continuously spins until it successfully acquires the lock,
   * making it suitable for scenarios with very short critical sections.
   */
  void Lock() noexcept
  {
    while (atomic_flag_.test_and_set(std::memory_order_acquire))
    {
    }
  }

  /**
   * @brief 释放锁 / Releases the lock
   *
   * 该方法清除锁标志，使其他线程可以获取锁。
   * This method clears the lock flag, allowing other threads to acquire the lock.
   */
  void Unlock() noexcept { atomic_flag_.clear(std::memory_order_release); }
};

}  // namespace LibXR
