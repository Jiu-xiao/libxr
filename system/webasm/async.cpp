#include "async.hpp"

#include "libxr_def.hpp"
#include "thread.hpp"

using namespace LibXR;

ASync::ASync(size_t stack_depth, Thread::Priority priority)
{
  UNUSED(stack_depth);
  UNUSED(priority);
}

ErrorCode ASync::AssignJob(Job job)
{
  Status expected = Status::READY;
  if (!status_.compare_exchange_strong(expected, Status::BUSY,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed))
  {
    return ErrorCode::BUSY;
  }

  job.Run(false, this);
  return ErrorCode::OK;
}
