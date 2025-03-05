#pragma once

#include <cstring>
#include <utility>

namespace LibXR {

template <typename ArgType, typename... Args>
class CallbackBlock {
 public:
  using FunctionType = void (*)(bool, ArgType, Args...);

  template <typename FunType, typename ArgT>
  CallbackBlock(FunType fun, ArgT &&arg)
      : fun_(fun), arg_(std::forward<ArgT>(arg)) {}

  void Call(bool in_isr, Args &&...args) {
    in_isr_ = in_isr;
    if (fun_) {
      fun_(in_isr, arg_, std::forward<Args>(args)...);
    }
  }

  CallbackBlock(const CallbackBlock &other) = delete;
  CallbackBlock &operator=(const CallbackBlock &other) = delete;

  CallbackBlock(CallbackBlock &&other) noexcept
      : fun_(std::exchange(other.fun_, nullptr)),
        arg_(std::move(other.arg_)),
        in_isr_(other.in_isr_) {}

  CallbackBlock &operator=(CallbackBlock &&other) noexcept {
    if (this != &other) {
      fun_ = std::exchange(other.fun_, nullptr);
      arg_ = std::move(other.arg_);
      in_isr_ = other.in_isr_;
    }
    return *this;
  }

 private:
  void (*fun_)(bool, ArgType, Args...);
  ArgType arg_;
  bool in_isr_ = false;
};

template <typename... Args>
class Callback {
 public:
  template <typename FunType, typename ArgType>
  static Callback Create(FunType fun, ArgType arg) {
    void (*fun_ptr)(bool, ArgType, Args...) = fun;
    auto cb_block = new CallbackBlock<ArgType, Args...>(fun_ptr, arg);

    auto cb_fun = [](bool in_isr, void *cb_block, Args... args) {
      auto *cb = static_cast<CallbackBlock<ArgType, Args...> *>(cb_block);
      cb->Call(in_isr, std::forward<Args>(args)...);
    };

    return Callback(cb_block, cb_fun);
  }

  Callback() : cb_block_(nullptr), cb_fun_(nullptr) {}

  Callback(const Callback &) = default;
  Callback &operator=(const Callback &) = default;

  Callback(Callback &&other) noexcept
      : cb_block_(std::exchange(other.cb_block_, nullptr)),
        cb_fun_(std::exchange(other.cb_fun_, nullptr)) {}

  Callback &operator=(Callback &&other) noexcept {
    if (this != &other) {
      cb_block_ = std::exchange(other.cb_block_, nullptr);
      cb_fun_ = std::exchange(other.cb_fun_, nullptr);
    }
    return *this;
  }

  template <typename... PassArgs>
  void Run(bool in_isr, PassArgs &&...args) const {
    if (cb_fun_) {
      cb_fun_(in_isr, cb_block_, std::forward<PassArgs>(args)...);
    }
  }

 private:
  Callback(void *cb_block, void (*cb_fun)(bool, void *, Args...))
      : cb_block_(cb_block), cb_fun_(cb_fun) {}

  void *cb_block_;
  void (*cb_fun_)(bool, void *, Args...);
};

}  // namespace LibXR