#pragma once

#include <cstring>
#include <tuple>
#include <utility>

#include "libxr_def.hpp"

namespace LibXR
{

template <typename... Args>
struct CallbackBlockHeader
{
  using InvokeFunType = void (*)(void*, bool, Args...);

  InvokeFunType run_fun_ = nullptr;
};

/**
 * @brief 回调函数封装块，提供参数绑定与擦除调用入口 / Callback block with bound argument
 * and erased invoke entry
 *
 * @tparam ArgType 绑定的第一个参数类型 / Type of the first bound argument
 * @tparam Args 额外的参数类型列表 / Additional argument types
 */
template <typename ArgType, typename... Args>
class CallbackBlock : public CallbackBlockHeader<Args...>
{
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
      : CallbackBlockHeader<Args...>{&InvokeThunk},
        fun_(std::forward<FunType>(fun)),
        arg_(std::forward<ArgT>(arg))
  {
  }

  /**
   * @brief 禁用拷贝构造与拷贝赋值 / Copy construction and copy assignment are disabled
   */
  CallbackBlock(const CallbackBlock& other) = delete;
  CallbackBlock& operator=(const CallbackBlock& other) = delete;

  static void InvokeThunk(void* cb_block, bool in_isr, Args... args)
  {
    auto* cb = static_cast<CallbackBlock<ArgType, Args...>*>(cb_block);
    if (!cb->fun_)
    {
      return;
    }
    cb->fun_(in_isr, cb->arg_, std::forward<Args>(args)...);
  }

 private:
  void (*fun_)(bool, ArgType, Args...);  ///< 绑定的回调函数 / Bound callback function
  ArgType arg_;                          ///< 绑定的参数 / Bound argument
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
  static void FunctionDefault(void*, bool, Args...) {}

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
    return Callback(cb_block);
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
  Callback(Callback&& other) noexcept : cb_block_(std::exchange(other.cb_block_, nullptr))
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
    }
    return *this;
  }

  template <typename... PassArgs>
  void Run(bool in_isr, PassArgs&&... args) const
  {
    if (cb_block_ == nullptr)
    {
      return;
    }
    cb_block_->run_fun_(cb_block_, in_isr, std::forward<PassArgs>(args)...);
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
  explicit Callback(CallbackBlockHeader<Args...>* cb_block) : cb_block_(cb_block) {}

  CallbackBlockHeader<Args...>* cb_block_ =
      nullptr;  ///< 回调块指针 / Pointer to the callback block
};

template <class CallbackT>
class CallbackGuard;

template <typename... Args>
class CallbackGuard<Callback<Args...>>
{
 public:
  using CallbackType = Callback<Args...>;

  template <typename... PassArgs>
  void Run(CallbackType& cb, bool in_isr, PassArgs&&... args)
  {
    if (cb.Empty())
    {
      return;
    }

    if (!running_)
    {
      running_ = true;
      auto cur_args = std::tuple<std::decay_t<Args>...>{std::forward<PassArgs>(args)...};
      do
      {
        pending_ = false;
        std::apply([&](auto&... a) { cb.Run(in_isr, a...); }, cur_args);
        if (pending_)
        {
          cur_args = pending_args_;
        }
      } while (pending_);
      running_ = false;
      return;
    }

    pending_args_ = std::tuple<std::decay_t<Args>...>{std::forward<PassArgs>(args)...};
    pending_ = true;
  }

 private:
  bool running_ = false;
  bool pending_ = false;
  std::tuple<std::decay_t<Args>...> pending_args_{};
};

}  // namespace LibXR
