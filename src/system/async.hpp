#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "semaphore.hpp"
#include "thread.hpp"

namespace LibXR {
class ASync {
 public:
  enum class Status : uint8_t {
    REDAY,
    BUSY,
    DONE,
  };

  ASync(size_t stack_depth, Thread::Priority priority);

  static void ThreadFun(ASync *async) {
    while (true) {
      if (async->sem_.Wait() == ErrorCode::OK) {
        async->job_.Run(false, async);
        async->status_ = Status::DONE;
      }
    }
  }

  Status status_ = Status::REDAY;

  Status GetStatus() {
    if (status_ != Status::DONE) {
      return status_;
    } else {
      status_ = Status::REDAY;
      return Status::DONE;
    }
  }

  ErrorCode AssignJob(Callback<ASync *> job);

  void AssignJobFromCallback(Callback<ASync *> job, bool in_isr) {
    job_ = job;
    status_ = Status::BUSY;
    sem_.PostFromCallback(in_isr);
  }

  Callback<ASync *> job_;
  Semaphore sem_;

  Thread thread_handle_;
};
}  // namespace LibXR
