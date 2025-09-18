#pragma once

#include <cstring>
#include <utility>

namespace LibXR
{

/**
 * @class CallbackBlock
 * @brief 提供一个回调函数的封装，实现参数绑定和回调执行。
 *        Provides a wrapper for callback functions, enabling argument binding and
 * invocation.
 *
 * @tparam ArgType 绑定的第一个参数类型。
 *         The type of the first bound argument.
 * @tparam Args 额外的参数类型列表。
 *         Additional argument types.
 */
template <typename ArgType, typename... Args>
class CallbackBlock
{
 public:
  /**
   * @typedef FunctionType
   * @brief 定义回调函数类型。
   *        Defines the type of the callback function.
   */
  using FunctionType = void (*)(bool, ArgType, Args...);

  /**
   * @brief 构造回调块，绑定回调函数和参数。
   *        Constructs a callback block, binding a function and an argument.
   *
   * @tparam FunType 函数类型。
   *         The type of the function.
   * @tparam ArgT 绑定的参数类型。
   *         The type of the bound argument.
   * @param fun 需要调用的回调函数。
   *        The callback function to be invoked.
   * @param arg 绑定的参数值。
   *        The bound argument value.
   */
  template <typename FunType, typename ArgT>
  CallbackBlock(FunType &&fun, ArgT &&arg)
      : fun_(std::forward<FunType>(fun)), arg_(std::forward<ArgT>(arg))
  {
  }

  /**
   * @brief 调用回调函数，并传递额外参数。
   *        Calls the callback function, passing additional arguments.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *        Indicates whether the call is executed within an interrupt context.
   * @param args 额外的参数。
   *        Additional arguments.
   */
  void Call(bool in_isr, Args &&...args)
  {
    in_isr_ = in_isr;
    if (fun_)
    {
      fun_(in_isr, arg_, std::forward<Args>(args)...);
    }
  }

  // 禁用拷贝构造与拷贝赋值
  // Copy construction and copy assignment are disabled.
  CallbackBlock(const CallbackBlock &other) = delete;
  CallbackBlock &operator=(const CallbackBlock &other) = delete;

  /**
   * @brief 移动构造函数，转移回调函数与参数。
   *        Move constructor, transferring the callback function and argument.
   *
   * @param other 另一个 CallbackBlock 实例。
   *        Another instance of CallbackBlock.
   */
  CallbackBlock(CallbackBlock &&other) noexcept
      : fun_(std::exchange(other.fun_, nullptr)),
        arg_(std::move(other.arg_)),
        in_isr_(other.in_isr_)
  {
  }

  /**
   * @brief 移动赋值运算符，转移回调函数与参数。
   *        Move assignment operator, transferring the callback function and argument.
   *
   * @param other 另一个 CallbackBlock 实例。
   *        Another instance of CallbackBlock.
   * @return 当前对象的引用。
   *         Reference to the current object.
   */
  CallbackBlock &operator=(CallbackBlock &&other) noexcept
  {
    if (this != &other)
    {
      fun_ = std::exchange(other.fun_, nullptr);
      arg_ = std::move(other.arg_);
    }
    return *this;
  }

 private:
  void (*fun_)(bool, ArgType,
               Args...);  ///< 绑定的回调函数。 The bound callback function.
  ArgType arg_;           ///< 绑定的参数。 The bound argument.
  bool in_isr_ = false;   ///< 指示是否在中断上下文中执行。 Indicates whether the function
                          ///< is executed in an ISR context.
};

/**
 * @class Callback
 * @brief 提供一个通用的回调包装，支持动态参数传递。
 *        Provides a generic callback wrapper, supporting dynamic argument passing.
 *
 * @tparam Args 额外的参数类型列表。
 *         Additional argument types.
 */
template <typename... Args>
class Callback
{
 public:
  /**
   * @brief 创建一个新的回调对象，并绑定回调函数和参数。
   *        Creates a new callback instance, binding a function and an argument.
   *
   * @tparam FunType 回调函数类型。
   *         Type of the callback function.
   * @tparam ArgType 绑定的参数类型。
   *         Type of the bound argument.
   * @param fun 需要绑定的回调函数。
   *        The callback function to bind.
   * @param arg 绑定的参数值。
   *        The bound argument value.
   * @return 生成的 Callback 实例。
   *         The created Callback instance.
   */
  template <typename FunType, typename ArgType>
  [[nodiscard]] static Callback Create(FunType fun, ArgType arg)
  {
    void (*fun_ptr)(bool, ArgType, Args...) = fun;
    auto cb_block = new CallbackBlock<ArgType, Args...>(fun_ptr, arg);

    auto cb_fun = [](bool in_isr, void *cb_block, Args... args)
    {
      auto *cb = static_cast<CallbackBlock<ArgType, Args...> *>(cb_block);
      cb->Call(in_isr, std::forward<Args>(args)...);
    };

    return Callback(cb_block, cb_fun);
  }

  /**
   * @brief 默认构造函数，创建空回调对象。
   *        Default constructor, creating an empty callback instance.
   */
  Callback() : cb_block_(nullptr), cb_fun_(nullptr) {}

  Callback(const Callback &) = default;
  Callback &operator=(const Callback &) = default;

  /**
   * @brief 移动构造函数，转移回调对象的所有权。
   *        Move constructor, transferring ownership of the callback object.
   *
   * @param other 另一个 Callback 实例。
   *        Another instance of Callback.
   */
  Callback(Callback &&other) noexcept
      : cb_block_(std::exchange(other.cb_block_, nullptr)),
        cb_fun_(std::exchange(other.cb_fun_, nullptr))
  {
  }

  /**
   * @brief 移动赋值运算符，转移回调对象的所有权。
   *        Move assignment operator, transferring ownership of the callback object.
   *
   * @param other 另一个 Callback 实例。
   *        Another instance of Callback.
   * @return 当前对象的引用。
   *         Reference to the current object.
   */
  Callback &operator=(Callback &&other) noexcept
  {
    if (this != &other)
    {
      cb_block_ = std::exchange(other.cb_block_, nullptr);
      cb_fun_ = std::exchange(other.cb_fun_, nullptr);
    }
    return *this;
  }

  /**
   * @brief 执行回调函数，并传递参数。
   *        Executes the callback function, passing the arguments.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *        Indicates whether the call is executed within an ISR context.
   * @param args 额外传递的参数。
   *        Additional arguments to pass.
   */
  template <typename... PassArgs>
  void Run(bool in_isr, PassArgs &&...args) const
  {
    if (cb_fun_)
    {
      cb_fun_(in_isr, cb_block_, std::forward<PassArgs>(args)...);
    }
  }

  /**
   * @brief 检查回调是否为空。
   *        Checks if the callback is empty.
   *
   * @return true
   * @return false
   */
  bool Empty() const { return cb_block_ == nullptr || cb_fun_ == nullptr; }

 private:
  /**
   * @brief 私有构造函数，仅用于内部创建回调实例。
   *        Private constructor, used internally for creating callback instances.
   *
   * @param cb_block 绑定的回调块对象。
   *        The bound callback block object.
   * @param cb_fun 处理回调调用的函数指针。
   *        Function pointer for handling callback invocation.
   */
  Callback(void *cb_block, void (*cb_fun)(bool, void *, Args...))
      : cb_block_(cb_block), cb_fun_(cb_fun)
  {
  }

  void *cb_block_;  ///< 存储回调块的指针。 Pointer to the callback block.
  void (*cb_fun_)(
      bool, void *,
      Args...);  ///< 存储回调执行的函数指针。 Pointer to the callback execution function.
};

}  // namespace LibXR
