#include "async.hpp"

#include "timer.hpp"

using namespace LibXR;

ASync::ASync(size_t stack_depth, Thread::Priority priority)
{
#ifdef LIBXR_NOT_SUPPORT_MUTI_THREAD
  UNUSED(stack_depth);
  UNUSED(priority);
  auto timer = Timer::CreateTask<ASync*>(
      [](ASync* async)
      {
        if (async->pending_.exchange(0U, std::memory_order_acquire) != 0U)
        {
          async->RunJob();
        }
      },
      this, 1);
  Timer::Start(timer);
  Timer::Add(timer);
#else
  thread_handle_.Create(this, ThreadFun, "async_job", stack_depth, priority);
#endif
}

ErrorCode ASync::AssignJob(Job job) { return AssignJobFromCallback(job, false); }

ErrorCode ASync::AssignJobFromCallback(Job job, bool in_isr)
{
  Status expected = Status::READY;
  if (!status_.compare_exchange_strong(expected, Status::BUSY, std::memory_order_acquire,
                                       std::memory_order_relaxed))
  {
    return ErrorCode::BUSY;
  }

  job_ = job;
#ifdef LIBXR_NOT_SUPPORT_MUTI_THREAD
  UNUSED(in_isr);
  pending_.store(1U, std::memory_order_release);
#else
  sem_.PostFromCallback(in_isr);
#endif
  return ErrorCode::OK;
}

void ASync::RunJob()
{
  job_.Run(false, this);
  status_.store(Status::DONE, std::memory_order_release);
}

void ASync::ThreadFun(ASync* async)
{
  while (true)
  {
    if (async->sem_.Wait() == ErrorCode::OK)
    {
      async->RunJob();
    }
  }
}
