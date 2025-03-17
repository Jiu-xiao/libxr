#pragma once

#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR
{

/**
 * @brief 互斥锁类，提供线程同步机制
 *        (Mutex class providing thread synchronization mechanisms).
 *
 * This class implements a mutex for thread-safe operations, supporting
 * locking, unlocking, and special handling for interrupt service routines (ISR).
 * 该类实现了一个互斥锁，用于确保多线程环境下的线程安全，支持加锁、解锁，并对中断服务程序（ISR）进行特殊处理。
 */
class Mutex
{
 public:
  /**
   * @brief 构造函数，初始化互斥锁
   *        (Constructor to initialize the mutex).
   */
  Mutex();

  /**
   * @brief 析构函数，销毁互斥锁
   *        (Destructor to destroy the mutex).
   */
  ~Mutex();

  /**
   * @brief 加锁，如果锁已被占用，则阻塞等待
   *        (Lock the mutex, blocking if it is already locked).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   */
  ErrorCode Lock();

  /**
   * @brief 尝试加锁，如果锁已被占用，则立即返回失败
   *        (Attempt to lock the mutex, returning immediately if already locked).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::BUSY` 表示锁已被占用 (`ErrorCode::BUSY` if the mutex is already
   * locked).
   */
  ErrorCode TryLock();

  /**
   * @brief 解锁互斥锁
   *        (Unlock the mutex).
   */
  void Unlock();

  /**
   * @brief 在回调（ISR）中尝试加锁
   *        (Attempt to lock the mutex inside an interrupt service routine (ISR)).
   * @param in_isr 指示当前是否处于中断上下文 (Indicates whether the function is called
   * inside an ISR).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::BUSY` 表示锁已被占用 (`ErrorCode::BUSY` if the mutex is already
   * locked).
   */
  ErrorCode TryLockInCallback(bool in_isr);

  /**
   * @brief 在回调（ISR）中解锁
   *        (Unlock the mutex inside an interrupt service routine (ISR)).
   * @param in_isr 指示当前是否处于中断上下文 (Indicates whether the function is called
   * inside an ISR).
   */
  void UnlockFromCallback(bool in_isr);

  /**
   * @brief 回调上下文中的锁守卫
   *        (Lock guard for usage inside callback/ISR).
   *
   * This class provides an RAII-style mechanism to automatically acquire
   * and release a lock inside an ISR.
   * 该类提供 RAII 风格的机制，自动在 ISR 中获取并释放锁。
   */
  class LockGuardInCallback;

  /**
   * @brief 互斥锁的 RAII 机制封装
   *        (RAII-style mechanism for automatic mutex management).
   *
   * This class ensures that a mutex is locked upon construction and
   * automatically released upon destruction.
   * 该类确保在构造时自动加锁，并在析构时自动释放锁。
   */
  class LockGuard
  {
   public:
    /**
     * @brief 构造函数，自动加锁
     *        (Constructor automatically locking the mutex).
     * @param mutex 需要管理的互斥锁 (Reference to the mutex to manage).
     */
    LockGuard(Mutex &mutex) : mutex_(mutex) { mutex_.Lock(); }

    /**
     * @brief 析构函数，自动解锁
     *        (Destructor automatically unlocking the mutex).
     */
    ~LockGuard() { mutex_.Unlock(); }

   private:
    Mutex &mutex_;  ///< 被管理的互斥锁 (Reference to the managed mutex).
  };

  /**
   * @brief 回调（ISR）上下文中的互斥锁管理类
   *        (Lock management class for ISR context).
   *
   * This class attempts to acquire the lock inside an ISR and releases it
   * when the object goes out of scope.
   * 该类尝试在 ISR 中获取锁，并在对象销毁时释放锁。
   */
  class LockGuardInCallback
  {
   public:
    /**
     * @brief 构造函数，尝试在 ISR 上下文中加锁
     *        (Constructor attempting to lock the mutex inside ISR context).
     * @param mutex 需要管理的互斥锁 (Reference to the mutex to manage).
     * @param in_isr 指示当前是否处于 ISR 上下文 (Indicates if the function is in ISR
     * context).
     */
    LockGuardInCallback(Mutex &mutex, bool in_isr)
        : mutex_(mutex), success_(mutex_.TryLockInCallback(in_isr)), in_isr_(in_isr)
    {
    }

    /**
     * @brief 析构函数，自动释放锁（如果成功加锁）
     *        (Destructor automatically unlocking the mutex if successfully locked).
     */
    ~LockGuardInCallback()
    {
      if (success_ == ErrorCode::OK)
      {
        mutex_.UnlockFromCallback(in_isr_);
      }
    }

    /**
     * @brief 检查是否成功加锁
     *        (Check if the mutex was successfully locked).
     * @return 如果成功加锁，返回 `true`，否则返回 `false`
     *         (Returns `true` if successfully locked, otherwise `false`).
     */
    bool Locked() { return success_ == ErrorCode::OK; }

   private:
    Mutex &mutex_;       ///< 被管理的互斥锁 (Reference to the managed mutex).
    ErrorCode success_;  ///< 记录加锁的状态 (Stores the lock acquisition status).
    bool in_isr_;        ///< 记录是否处于 ISR 上下文 (Indicates if in ISR context).
  };

 private:
  libxr_mutex_handle mutex_handle_;  ///< 互斥锁句柄 (Handle for the mutex).
};

}  // namespace LibXR
