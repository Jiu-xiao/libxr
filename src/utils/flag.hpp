#pragma once

#include <atomic>
#include <cstdint>

namespace LibXR
{

/**
 * @brief 轻量标志位模块（Flag）/ Lightweight flag utilities for state signaling
 *
 * @details
 *        提供原子/非原子两种标志位实现，以及作用域辅助器（RAII）。
 *        Provides atomic and non-atomic flags with RAII helpers.
 *
 * @note 该模块不是互斥锁/自旋锁，不提供临界区互斥语义 / Not a mutex/spinlock; no mutual
 * exclusion semantics
 */
namespace Flag
{

/**
 * @brief 原子标志位 / Atomic flag
 *
 * @details
 *        提供 Set/Clear/IsSet 与 TestAndSet/TestAndClear/Exchange，可用于多线程/多核/ISR
 *        共享的状态标记（不提供互斥锁语义）。
 *        Provides Set/Clear/IsSet and TestAndSet/TestAndClear/Exchange for state
 * signaling across threads/cores/ISRs (no mutex semantics).
 */
class Atomic
{
 public:
  /**
   * @brief 构造函数，默认未置位 / Constructor, default cleared
   */
  Atomic() = default;

  /**
   * @brief 置位标志 / Set the flag
   */
  void Set() noexcept { value_.store(1u, std::memory_order_release); }

  /**
   * @brief 清除标志 / Clear the flag
   */
  void Clear() noexcept { value_.store(0u, std::memory_order_release); }

  /**
   * @brief 判断是否已置位 / Check whether the flag is set
   *
   * @return true 已置位 / Flag is set
   * @return false 未置位 / Flag is clear
   */
  [[nodiscard]] bool IsSet() const noexcept
  {
    return value_.load(std::memory_order_acquire) != 0u;
  }

  /**
   * @brief 测试并置位：置位并返回旧状态 / Test-and-set: set and return previous state
   *
   * @return true 先前已置位 / Was already set
   * @return false 先前未置位 / Was clear
   */
  [[nodiscard]] bool TestAndSet() noexcept
  {
    return value_.exchange(1u, std::memory_order_acq_rel) != 0u;
  }

  /**
   * @brief 测试并清除：清除并返回旧状态 / Test-and-clear: clear and return previous state
   *
   * @return true 先前已置位 / Was set
   * @return false 先前未置位 / Was clear
   */
  [[nodiscard]] bool TestAndClear() noexcept
  {
    return value_.exchange(0u, std::memory_order_acq_rel) != 0u;
  }

  /**
   * @brief 交换：写入指定值并返回旧状态 / Exchange: set to desired value and return
   * previous state
   *
   * @param set_value 期望写入的值 / Desired value to write
   * @return true 旧值为 set / Old value was set
   * @return false 旧值为 clear / Old value was clear
   */
  [[nodiscard]] bool Exchange(bool set_value) noexcept
  {
    return value_.exchange(set_value ? 1u : 0u, std::memory_order_acq_rel) != 0u;
  }

  /**
   * @brief 禁用拷贝构造与拷贝赋值 / Copy construction and copy assignment are disabled
   */
  Atomic(const Atomic&) = delete;
  Atomic& operator=(const Atomic&) = delete;

 private:
  std::atomic<uint8_t> value_{
      0u};  ///< 标志值（0=clear, 1=set）/ Flag value (0=clear, 1=set)
};

/**
 * @brief 普通标志位（非原子）/ Non-atomic flag
 *
 * @details
 *        不具备并发安全性；仅适用于单线程环境或外部已保证互斥/临界区保护的场景。
 *        Not thread-safe; use only in single-threaded context or when externally
 * synchronized.
 */
class Plain
{
 public:
  /**
   * @brief 构造函数，默认未置位 / Constructor, default cleared
   */
  Plain() = default;

  /**
   * @brief 置位标志 / Set the flag
   */
  void Set() noexcept { value_ = true; }

  /**
   * @brief 清除标志 / Clear the flag
   */
  void Clear() noexcept { value_ = false; }

  /**
   * @brief 判断是否已置位 / Check whether the flag is set
   *
   * @return true 已置位 / Flag is set
   * @return false 未置位 / Flag is clear
   */
  [[nodiscard]] bool IsSet() const noexcept { return value_; }

  /**
   * @brief 测试并置位：置位并返回旧状态 / Test-and-set: set and return previous state
   *
   * @return true 先前已置位 / Was already set
   * @return false 先前未置位 / Was clear
   */
  [[nodiscard]] bool TestAndSet() noexcept
  {
    bool old = value_;
    value_ = true;
    return old;
  }

  /**
   * @brief 测试并清除：清除并返回旧状态 / Test-and-clear: clear and return previous state
   *
   * @return true 先前已置位 / Was set
   * @return false 先前未置位 / Was clear
   */
  [[nodiscard]] bool TestAndClear() noexcept
  {
    bool old = value_;
    value_ = false;
    return old;
  }

  /**
   * @brief 交换：写入指定值并返回旧状态 / Exchange: set to desired value and return
   * previous state
   *
   * @param set_value 期望写入的值 / Desired value to write
   * @return true 旧值为 set / Old value was set
   * @return false 旧值为 clear / Old value was clear
   */
  [[nodiscard]] bool Exchange(bool set_value) noexcept
  {
    bool old = value_;
    value_ = set_value;
    return old;
  }

 private:
  bool value_{
      false};  ///< 标志值（false=clear, true=set）/ Flag value (false=clear, true=set)
};

/**
 * @brief 作用域标志管理器：构造时写入指定值，析构时恢复原值 / Scoped flag restorer: set
 * on entry, restore on exit
 *
 * @details
 *        适用于“进入作用域临时改变 flag，退出恢复”的场景。
 *        Useful when temporarily changing a flag within a scope and restoring it on exit.
 *
 * @tparam FlagT 支持 Exchange(bool)->bool 的标志类型 / Flag type that provides
 * Exchange(bool)->bool
 */
template <typename FlagT>
class ScopedRestore
{
 public:
  /**
   * @brief 构造函数：写入 set_value 并保存旧值 / Constructor: set set_value and store
   * previous value
   *
   * @param flag 需要管理的标志对象 / Flag instance to manage
   * @param set_value 构造时写入的值 / Value to set on construction
   */
  explicit ScopedRestore(FlagT& flag, bool set_value = true)
      : flag_(flag), prev_(flag_.Exchange(set_value))
  {
  }

  /**
   * @brief 析构函数：恢复旧值 / Destructor: restore previous value
   */
  ~ScopedRestore() { (void)flag_.Exchange(prev_); }

  /**
   * @brief 禁用拷贝构造与拷贝赋值 / Copy construction and copy assignment are disabled
   */
  ScopedRestore(const ScopedRestore&) = delete;
  ScopedRestore& operator=(const ScopedRestore&) = delete;

 private:
  FlagT& flag_;  ///< 被管理的标志对象 / Managed flag instance
  bool prev_;    ///< 进入作用域前的旧值 / Previous value before entering scope
};

}  // namespace Flag
}  // namespace LibXR
