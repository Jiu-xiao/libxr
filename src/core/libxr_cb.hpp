#pragma once

#include <cstring>
#include <tuple>
#include <type_traits>
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
   * 工厂层已经把 `fun` 收窄到了当前块真实需要的函数指针类型，因此这里不再保留
   * 额外的模板参数做二次推导。
   * The factory has already narrowed `fun` to the exact function-pointer type required
   * by this block, so there is no need to keep another template parameter here.
   *
   * @param fun 需要调用的回调函数 / Callback function to be invoked
   * @param arg 绑定的参数值 / Bound argument value
   */
  CallbackBlock(FunctionType fun, ArgType&& arg)
      : CallbackBlockHeader<Args...>{&InvokeThunk}, fun_(fun), arg_(std::move(arg))
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
    cb->Invoke(in_isr, std::forward<Args>(args)...);
  }

 protected:
  void Invoke(bool in_isr, Args... args)
  {
    if (!fun_)
    {
      return;
    }
    fun_(in_isr, arg_, std::forward<Args>(args)...);
  }

  FunctionType fun_;  ///< 绑定的回调函数 / Bound callback function
  ArgType arg_;       ///< 绑定的参数 / Bound argument
};

template <typename ArgType, typename... Args>
class GuardedCallbackBlock : public CallbackBlock<ArgType, Args...>
{
 public:
  /**
   * @brief 带防重入保护的回调块 / Callback block with reentry guard
   *
   * 与 `CallbackBlock` 一样，这里直接接收精确的函数指针类型。
   * Same as `CallbackBlock`, this constructor takes the exact callback function-pointer
   * type directly.
   */
  GuardedCallbackBlock(typename CallbackBlock<ArgType, Args...>::FunctionType fun,
                       ArgType&& arg)
      : CallbackBlock<ArgType, Args...>(fun, std::move(arg))
  {
    this->run_fun_ = &InvokeThunk;
  }

  static void InvokeThunk(void* cb_block, bool in_isr, Args... args)
  {
    auto* cb = static_cast<GuardedCallbackBlock<ArgType, Args...>*>(cb_block);

    if (!cb->running_)
    {
      cb->running_ = true;
      auto cur_args = std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...};
      do
      {
        cb->pending_ = false;
        std::apply([&](auto&... a) { cb->Invoke(in_isr, a...); }, cur_args);
        if (cb->pending_)
        {
          cur_args = cb->pending_args_;
        }
      } while (cb->pending_);
      cb->running_ = false;
      return;
    }

    // 重入时只保留最新一组参数，把递归调用压平成串行重放。
    // On reentry, keep only the latest argument pack so recursive callback chains are
    // flattened into serialized replay.
    cb->pending_args_ = std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...};
    cb->pending_ = true;
  }

 private:
  bool running_ = false;
  bool pending_ = false;
  std::tuple<std::decay_t<Args>...> pending_args_{};
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
  inline static CallbackBlockHeader<Args...> empty_cb_block_ = {&FunctionDefault};

 public:
  /**
   * @brief 创建回调对象并绑定回调函数与参数 / Create a callback instance with bound
   * function and argument
   *
   * @tparam ArgType 绑定参数类型 / Bound argument type
   * @param fun 需要绑定的回调函数 / Callback function to bind
   * @param arg 绑定的参数值 / Bound argument value
   * @return Callback 实例 / Created Callback instance
   *
   * `fun` 的类型被显式写成当前 `CallbackBlock` 的真实函数指针类型，
   * 同时再用 `std::type_identity_t` 阻止它参与额外推导；这样 `ArgType`
   * 只从 `arg` 推导，避免把“看起来能转”的别的函数指针类型错误地吸进来。
   * `fun` is written as the exact callback-function pointer type required by the current
   * `CallbackBlock`, and wrapped in `std::type_identity_t` so it does not participate in
   * extra deduction. This keeps `ArgType` deduced only from `arg`, and avoids accepting
   * unrelated-but-convertible function pointer types by accident.
   *
   * 因此调用方需要传入可转换到该精确函数指针类型的对象，例如普通函数、静态成员函数，
   * 或无捕获 lambda。
   * Callers therefore need an object convertible to that exact function-pointer type,
   * such as a free function, static member function, or non-capturing lambda.
   *
   * @note 该写法依赖 `std::type_identity_t`，要求编译环境提供 C++20 标准库。
   *       This form relies on `std::type_identity_t`, which requires a C++20 standard
   *       library.
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  template <typename ArgType>
  [[nodiscard]] static Callback Create(
      std::type_identity_t<typename CallbackBlock<ArgType, Args...>::FunctionType> fun,
      ArgType arg)
  {
    auto cb_block = new CallbackBlock<ArgType, Args...>(fun, std::move(arg));
    return Callback(cb_block);
  }

  /**
   * @brief 创建带防重入保护的回调 / Create a guarded callback
   *
   * 参数约束与 `Create` 完全一致，只是底层块换成了带重入压平逻辑的
   * `GuardedCallbackBlock`。
   * The parameter constraint is identical to `Create`; the only difference is that the
   * underlying block becomes `GuardedCallbackBlock`, which flattens reentrant callback
   * chains.
   */
  template <typename ArgType>
  [[nodiscard]] static Callback CreateGuarded(
      std::type_identity_t<typename CallbackBlock<ArgType, Args...>::FunctionType> fun,
      ArgType arg)
  {
    auto cb_block = new GuardedCallbackBlock<ArgType, Args...>(fun, std::move(arg));
    return Callback(cb_block);
  }

  /**
   * @brief 默认构造函数，创建空回调对象 / Default constructor creating an empty callback
   */
  Callback() : cb_block_(&empty_cb_block_) {}

  Callback(const Callback&) = default;
  Callback& operator=(const Callback&) = default;

  /**
   * @brief 移动构造函数，转移回调对象的所有权 / Move constructor transferring callback
   * ownership
   *
   * @param other 另一个 Callback 实例 / Another Callback instance
   */
  Callback(Callback&& other) noexcept
      : cb_block_(std::exchange(other.cb_block_, &empty_cb_block_))
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
      cb_block_ = std::exchange(other.cb_block_, &empty_cb_block_);
    }
    return *this;
  }

  template <typename... PassArgs>
  void Run(bool in_isr, PassArgs&&... args) const
  {
    cb_block_->run_fun_(cb_block_, in_isr, std::forward<PassArgs>(args)...);
  }

  /**
   * @brief 检查回调是否为空 / Check whether the callback is empty
   *
   * @return true 回调为空 / Callback is empty
   * @return false 回调非空 / Callback is not empty
   */
  bool Empty() const { return cb_block_ == &empty_cb_block_; }

 private:
  /**
   * @brief 私有构造函数，仅用于内部创建回调实例 / Private constructor used internally to
   * create callback instances
   *
   * @param cb_block 回调块对象指针 / Pointer to the callback block
   * @param cb_fun 回调执行函数指针 / Callback invocation function pointer
   */
  explicit Callback(CallbackBlockHeader<Args...>* cb_block)
      : cb_block_((cb_block != nullptr) ? cb_block : &empty_cb_block_)
  {
  }

  CallbackBlockHeader<Args...>* cb_block_ =
      &empty_cb_block_;  ///< 回调块指针 / Pointer to the callback block
};

}  // namespace LibXR
