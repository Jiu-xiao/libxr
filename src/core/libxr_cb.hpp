#pragma once

#include <array>
#include <utility>

#include "libxr_def.hpp"

namespace LibXR {
template <typename ResultType, typename ArgType, typename... Args>
class CallbackBlock {
public:
  CallbackBlock(ResultType (*fun)(bool, ArgType, Args... args), ArgType arg)
      : fun_(fun), arg_(arg) {}

  bool InISR() { return in_isr_; }

  ResultType (*fun_)(bool, ArgType, Args... args);
  ArgType arg_;
  bool in_isr_;
};

template <typename ResultType, typename... Args> class Callback {
public:
  template <typename FunType, typename ArgType>
  static Callback Create(FunType fun, ArgType arg) {
    /* Check the type of fun */
    ResultType (*fun_ptr)(bool, ArgType, Args...) = fun;

    auto cb_block =
        new CallbackBlock<ResultType, ArgType, Args...>(fun_ptr, arg);
    auto cb_fun = [](bool in_isr, void *cb_block, Args... args) {
      CallbackBlock<ResultType, ArgType, Args...> *cb =
          static_cast<CallbackBlock<ResultType, ArgType, Args...> *>(cb_block);
      cb->in_isr_ = in_isr;
      if (cb->fun_) {
        return cb->fun_(in_isr, cb->arg_, args...);
      }
    };

    return Callback(cb_block, cb_fun);
  }

  Callback() : cb_block_(NULL), cb_fun_(NULL) {}

  Callback(const Callback &cb) = default;

  const Callback operator=(const Callback &cb) {
    memcpy(this, &cb, sizeof(cb));
    return *this;
  }

  ResultType RunFromUser(Args... args) const {
    return cb_fun_(false, cb_block_, args...);
  }

  ResultType RunFromISR(Args... args) const {
    return cb_fun_(true, cb_block_, args...);
  }

  void *const cb_block_;
  ResultType (*const cb_fun_)(bool, void *, Args...);

private:
  Callback(void *cb_block, ResultType (*cb_fun)(bool, void *, Args...))
      : cb_block_(cb_block), cb_fun_(cb_fun) {}
};

} // namespace LibXR