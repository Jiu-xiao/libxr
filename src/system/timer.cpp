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

  while (true)
  {
    Thread::SleepUntil(time, 1);

    const MillisecondTimestamp current_time = Timebase::GetMilliseconds();
    const uint32_t elapsed = (current_time - last_refresh_time).ToMillisecond();
    if (elapsed != 0U)
    {
      last_refresh_time = current_time;
      Timer::Advance(elapsed);
    }

    time = Thread::GetTime();
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

void Timer::Advance(uint32_t elapsed)
{
  if (elapsed == 0U)
  {
    return;
  }

  if (!LibXR::Timer::list_)
  {
    LibXR::Timer::list_ = new LibXR::LockFreeList();

#ifndef LIBXR_NOT_SUPPORT_MUTI_THREAD

    auto thread_handle = Thread();
    thread_handle.Create<void*>(nullptr, RefreshThreadFunction, "libxr_timer_task", 512,
                                Thread::Priority::HIGH);
#endif
  }

  auto fun = [elapsed](ControlBlock& block)
  {
    if (!block.enable_)
    {
      return ErrorCode::OK;
    }

    const auto advance = Detail::AdvanceTimerCount(block.count_, block.cycle_, elapsed);
    block.count_ = advance.count;
    if (advance.due)
    {
      block.Run();
    }

    return ErrorCode::OK;
  };

  list_->Foreach<ControlBlock>(fun);
}

void Timer::Refresh() { Advance(1U); }
