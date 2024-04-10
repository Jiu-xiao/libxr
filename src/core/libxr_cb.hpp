#pragma once

#include <array>

#include "libxr_def.hpp"

namespace LibXR {
template <typename ResultType, typename ArgType, typename... Args>
class CallbackBlock {
public:
  CallbackBlock(ResultType (*fun)(ArgType, Args... args), ArgType arg)
      : fun_(fun), arg_(arg) {}

  bool InISR() { return in_isr_; }

  ResultType (*fun_)(ArgType, Args... args);
  ArgType arg_;
  bool in_isr_;
};

template <typename ResultType, typename... Args> class Callback {
public:
  template <typename ArgType>
  Callback(ResultType (*fun)(ArgType, Args... args), ArgType arg) {
    cb_block_ = new CallbackBlock<ResultType, ArgType, Args...>(fun, arg);
    cb_fun_ = [](bool in_isr, void *cb_block, Args... args) {
      CallbackBlock<ResultType, ArgType, Args...> *cb =
          static_cast<CallbackBlock<ResultType, ArgType, Args...> *>(cb_block);
      cb->in_isr_ = in_isr;
      return cb->fun_(cb->arg_, args...);
    };
  }

  Callback(Callback &cb) = default;

  ResultType RunFromUser(Args... args) {
    return cb_fun_(false, cb_block_, args...);
  }
  ResultType RunFromISR(Args... args) {
    return cb_fun_(true, cb_block_, args...);
  }

  void *cb_block_;
  ResultType (*cb_fun_)(bool, void *, Args...);
};

} // namespace LibXR