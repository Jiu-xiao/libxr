#pragma once

#include <cstring>
#include <tuple>
#include <utility>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @brief 回调函数封装块，提供重入保护与参数绑定 / Callback block with argument binding
 * and reentrancy guard
 *
 * @details
 *        当回调正在执行时再次触发（重入），不会递归调用回调函数，而是缓存一次“待执行请求”；
 *        待当前执行结束后在同一调用点以循环方式补跑，从而避免无限嵌套（trampoline
 * 扁平化）。 When reentered while running, the callback is not invoked recursively.
 * Instead, one pending request is cached and replayed in a loop after the current
 * invocation completes, flattening recursion via a trampoline-style execution.
 *
 * @tparam ArgType 绑定的第一个参数类型 / Type of the first bound argument
 * @tparam Args 额外的参数类型列表 / Additional argument types
 */
template <typename ArgType, typename... Args>
class CallbackBlock
{
  bool running_ = false;
  bool pending_ = false;
  std::tuple<std::decay_t<Args>...> pending_args_{};

 public:
  /**
   * @brief 回调函数类型定义 / Callback function type definition
   */
  using FunctionType = void (*)(bool, ArgType, Args...);

  /**
   * @brief 构造回调块，绑定回调函数与参数 / Construct a callback block with bound
   * function and argument
   *
   * @tparam FunType 函数类型 / Function type
   * @tparam ArgT 绑定参数类型 / Bound argument type
   * @param fun 需要调用的回调函数 / Callback function to be invoked
   * @param arg 绑定的参数值 / Bound argument value
   */
  template <typename FunType, typename ArgT>
  CallbackBlock(FunType&& fun, ArgT&& arg)
      : fun_(std::forward<FunType>(fun)), arg_(std::forward<ArgT>(arg))
  {
  }

  /**
   * @brief 触发回调执行（带重入保护） / Trigger callback execution with reentrancy guard
   *
   * @param in_isr 是否在中断上下文中执行 / Whether executed in ISR context
   * @param args 额外参数 / Additional arguments
   *
   * @note
   *       若在执行过程中发生重入，本次重入不会递归执行回调，而是写入待执行参数并置
   * pending 标志；当前执行结束后将补跑一次。
   *       On reentry, the callback is not invoked recursively. A pending flag is set and
   * arguments are stored for a single replay after the current run.
   */
  void Call(bool in_isr, Args... args)
  {
    if (!fun_)
    {
      return;
    }

    if (!running_)
    {
      running_ = true;

      auto cur_args = std::tuple<std::decay_t<Args>...>{args...};

      do
      {
        pending_ = false;
        std::apply([&](auto&... a) { fun_(in_isr, arg_, a...); }, cur_args);

        if (pending_)
        {
          cur_args = pending_args_;  // overwrite pending args on reentry
        }
      } while (pending_);

      running_ = false;
      return;
    }

    // reentrant: cache one pending request (overwrite)
    pending_args_ = std::tuple<std::decay_t<Args>...>{args...};
    pending_ = true;
  }

  /**
   * @brief 禁用拷贝构造与拷贝赋值 / Copy construction and copy assignment are disabled
   */
  CallbackBlock(const CallbackBlock& other) = delete;
  CallbackBlock& operator=(const CallbackBlock& other) = delete;

  /**
   * @brief 移动构造函数，转移回调函数与参数 / Move constructor transferring function and
   * argument
   *
   * @param other 另一个 CallbackBlock 实例 / Another CallbackBlock instance
   */
  CallbackBlock(CallbackBlock&& other) noexcept
      : fun_(std::exchange(other.fun_, nullptr)),
        arg_(std::move(other.arg_)),
        in_isr_(other.in_isr_)
  {
  }

  /**
   * @brief 移动赋值运算符，转移回调函数与参数 / Move assignment operator transferring
   * function and argument
   *
   * @param other 另一个 CallbackBlock 实例 / Another CallbackBlock instance
   * @return 当前对象引用 / Reference to the current object
   */
  CallbackBlock& operator=(CallbackBlock&& other) noexcept
  {
    if (this != &other)
    {
      fun_ = std::exchange(other.fun_, nullptr);
      arg_ = std::move(other.arg_);
      in_isr_ = other.in_isr_;
    }
    return *this;
  }

 private:
  void (*fun_)(bool, ArgType, Args...);  ///< 绑定的回调函数 / Bound callback function
  ArgType arg_;                          ///< 绑定的参数 / Bound argument
  bool in_isr_ = false;  ///< 是否在中断上下文中执行 / Whether executed in ISR context
};

/**
 * @brief 通用回调包装，支持动态参数传递 / Generic callback wrapper supporting dynamic
 * argument passing
 *
 * @tparam Args 额外的参数类型列表 / Additional argument types
 */
template <typename... Args>
class Callback
{
  static void FunctionDefault(bool, void*, Args...) {}

 public:
  /**
   * @brief 创建回调对象并绑定回调函数与参数 / Create a callback instance with bound
   * function and argument
   *
   * @tparam FunType 回调函数类型 / Callback function type
   * @tparam ArgType 绑定参数类型 / Bound argument type
   * @param fun 需要绑定的回调函数 / Callback function to bind
   * @param arg 绑定的参数值 / Bound argument value
   * @return Callback 实例 / Created Callback instance
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  template <typename FunType, typename ArgType>
  [[nodiscard]] static Callback Create(FunType fun, ArgType arg)
  {
    void (*fun_ptr)(bool, ArgType, Args...) = fun;
    auto cb_block = new CallbackBlock<ArgType, Args...>(fun_ptr, arg);

    auto cb_fun = [](bool in_isr, void* cb_block, Args... args)
    {
      auto* cb = static_cast<CallbackBlock<ArgType, Args...>*>(cb_block);
      cb->Call(in_isr, std::forward<Args>(args)...);
    };

    return Callback(cb_block, cb_fun);
  }

  /**
   * @brief 默认构造函数，创建空回调对象 / Default constructor creating an empty callback
   */
  Callback() {}

  Callback(const Callback&) = default;
  Callback& operator=(const Callback&) = default;

  /**
   * @brief 移动构造函数，转移回调对象的所有权 / Move constructor transferring callback
   * ownership
   *
   * @param other 另一个 Callback 实例 / Another Callback instance
   */
  Callback(Callback&& other) noexcept
      : cb_block_(std::exchange(other.cb_block_, nullptr)),
        cb_fun_(std::exchange(other.cb_fun_, nullptr))
  {
  }

  /**
   * @brief 移动赋值运算符，转移回调对象的所有权 / Move assignment operator transferring
   * callback ownership
   *
   * @param other 另一个 Callback 实例 / Another Callback instance
   * @return 当前对象引用 / Reference to the current object
   */
  Callback& operator=(Callback&& other) noexcept
  {
    if (this != &other)
    {
      cb_block_ = std::exchange(other.cb_block_, nullptr);
      cb_fun_ = std::exchange(other.cb_fun_, nullptr);
    }
    return *this;
  }

  /**
   * @brief 执行回调函数并传递参数 / Execute the callback with arguments
   *
   * @param in_isr 是否在中断上下文中执行 / Whether executed in ISR context
   * @param args 额外传递的参数 / Additional arguments to pass
   */
  template <typename... PassArgs>
  void Run(bool in_isr, PassArgs&&... args) const
  {
    cb_fun_(in_isr, cb_block_, std::forward<PassArgs>(args)...);
  }

  /**
   * @brief 检查回调是否为空 / Check whether the callback is empty
   *
   * @return true 回调为空 / Callback is empty
   * @return false 回调非空 / Callback is not empty
   */
  bool Empty() const { return cb_block_ == nullptr; }

 private:
  /**
   * @brief 私有构造函数，仅用于内部创建回调实例 / Private constructor used internally to
   * create callback instances
   *
   * @param cb_block 回调块对象指针 / Pointer to the callback block
   * @param cb_fun 回调执行函数指针 / Callback invocation function pointer
   */
  Callback(void* cb_block, void (*cb_fun)(bool, void*, Args...))
      : cb_block_(cb_block), cb_fun_(cb_fun)
  {
  }

  void* cb_block_ = nullptr;  ///< 回调块指针 / Pointer to the callback block
  void (*cb_fun_)(bool, void*, Args...) =
      FunctionDefault;  ///< 回调执行函数指针 / Callback invocation function pointer
};

}  // namespace LibXR
