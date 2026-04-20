#include "async.hpp"

#include "libxr_def.hpp"
#include "thread.hpp"

using namespace LibXR;

ASync::ASync(size_t stack_depth, Thread::Priority priority)
{
  thread_handle_.Create(this, ThreadFun, "async_job", stack_depth, priority);
}

ErrorCode ASync::AssignJob(Job job)
{
  Status expected = Status::READY;
  if (!status_.compare_exchange_strong(expected, Status::BUSY))
  {
    return ErrorCode::BUSY;
  }

  job_ = job;
  sem_.Post();

  return ErrorCode::OK;
}
