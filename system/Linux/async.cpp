#include "async.hpp"
#include "libxr_def.hpp"
#include "thread.hpp"

using namespace LibXR;

ASync::ASync(size_t stack_depth, Thread::Priority priority) {
  thread_handle_.Create(this, thread_fun, "async_job", stack_depth, priority);
}

ErrorCode ASync::AssignJob(Callback<ASync *> job) {
  if (status_ == Status::BUSY) {
    return ErrorCode::BUSY;
  }

  status_ = Status::BUSY;

  job_ = job;
  sem_.Post();

  return ErrorCode::OK;
}
