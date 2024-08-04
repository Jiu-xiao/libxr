#pragma once

#include <array>
#include <cstring>
#include <utility>

#include "libxr_def.hpp"

namespace LibXR {

template <typename ArgType, typename... Args> class CallbackBlock {
public:
  CallbackBlock(void (*fun)(bool, ArgType, Args...), ArgType arg)
      : fun_(fun), arg_(arg), in_isr_(false) {}

  void Call(bool in_isr, Args &&...args) {
    in_isr_ = in_isr;
    if (fun_) {
      fun_(in_isr, arg_, std::forward<Args>(args)...);
    }
  }

  CallbackBlock(const CallbackBlock &other)
      : fun_(other.fun_), arg_(other.arg_), in_isr_(other.in_isr_) {}

  CallbackBlock &operator=(const CallbackBlock &other) {
    if (this != &other) {
      fun_ = other.fun_;
      arg_ = other.arg_;
      in_isr_ = other.in_isr_;
    }
    return *this;
  }

private:
  void (*fun_)(bool, ArgType, Args...);
  ArgType arg_;
  bool in_isr_;
};

template <typename... Args> class Callback {
public:
  template <typename FunType, typename ArgType>
  static Callback Create(FunType fun, ArgType arg) {
    void (*fun_ptr)(bool, ArgType, Args...) = fun;

    auto cb_block = new CallbackBlock<ArgType, Args...>(fun_ptr, arg);

    auto cb_fun = [](bool in_isr, void *cb_block, Args... args) {
      CallbackBlock<ArgType, Args...> *cb =
          static_cast<CallbackBlock<ArgType, Args...> *>(cb_block);
      cb->Call(in_isr, std::forward<Args>(args)...);
    };

    return Callback(cb_block, cb_fun);
  }

  Callback() : cb_block_(nullptr), cb_fun_(nullptr) {}

  Callback(const Callback &) = default;

  Callback(Callback &&other)
      : cb_block_(other.cb_block_), cb_fun_(other.cb_fun_) {
    other.cb_block_ = nullptr;
    other.cb_fun_ = nullptr;
  }

  Callback &operator=(Callback &&other) {
    if (this != &other) {
      cb_block_ = other.cb_block_;
      cb_fun_ = other.cb_fun_;
      other.cb_block_ = nullptr;
      other.cb_fun_ = nullptr;
    }
    return *this;
  }

  Callback &operator=(const Callback &) = default;

  template <typename... PassArgs> void RunFromUser(PassArgs &&...args) const {
    cb_fun_(false, cb_block_, std::forward<PassArgs>(args)...);
  }

  template <typename... PassArgs> void RunFromISR(PassArgs &...args) const {
    cb_fun_(true, cb_block_, std::forward<Args>(args)...);
  }

private:
  Callback(void *cb_block, void (*cb_fun)(bool, void *, Args...))
      : cb_block_(cb_block), cb_fun_(cb_fun) {}

  void *cb_block_;
  void (*cb_fun_)(bool, void *, Args...);
};

} // namespace LibXR