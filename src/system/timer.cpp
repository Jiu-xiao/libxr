#include "timer.hpp"

#include "lockfree_list.hpp"
#include "timebase.hpp"

using namespace LibXR;

void Timer::Start(TimerHandle handle) { handle->data_.enable_ = true; }

void Timer::Stop(TimerHandle handle) { handle->data_.enable_ = false; }

void Timer::SetCycle(TimerHandle handle, uint32_t cycle)
{
  ASSERT(cycle > 0);
  handle->data_.cycle_ = cycle;
}

void Timer::RefreshThreadFunction(void*)
{
  ThreadTimestamp time = Thread::GetTime();
  MillisecondTimestamp last_refresh_time = Timebase::GetMilliseconds();
  Timer::Refresh();

  while (true)
  {
    Thread::SleepUntil(time, 1);

    const MillisecondTimestamp current_time = Timebase::GetMilliseconds();
    const uint32_t elapsed = (current_time - last_refresh_time).ToMillisecond();
    for (uint32_t i = 0; i < elapsed; ++i)
    {
      Timer::Refresh();
    }
    last_refresh_time = current_time;
  }
}

void Timer::Add(TimerHandle handle)
{
  ASSERT(!handle->next_);
  REQUIRE(Timebase::IsReady());

  if (!LibXR::Timer::list_)
  {
    LibXR::Timer::list_ = new LibXR::LockFreeList();
#ifdef LIBXR_NOT_SUPPORT_MUTI_THREAD
#else
    thread_handle_.Create<void*>(nullptr, RefreshThreadFunction, "libxr_timer_task",
                                 stack_depth_, priority_);
#endif
  }
  list_->Add(*handle);
}

void Timer::Refresh()
{
  if (!LibXR::Timer::list_)
  {
    LibXR::Timer::list_ = new LibXR::LockFreeList();

#ifndef LIBXR_NOT_SUPPORT_MUTI_THREAD

    auto thread_handle = Thread();
    thread_handle.Create<void*>(nullptr, RefreshThreadFunction, "libxr_timer_task", 512,
                                Thread::Priority::HIGH);
#endif
  }

  auto fun = [](ControlBlock& block)
  {
    if (!block.enable_)
    {
      return ErrorCode::OK;
    }

    block.count_++;

    if (block.count_ >= block.cycle_)
    {
      block.count_ = 0;
      block.Run();
    }

    return ErrorCode::OK;
  };

  list_->Foreach<ControlBlock>(fun);
}
