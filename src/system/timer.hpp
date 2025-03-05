#pragma once

#include <utility>

#include "libxr_def.hpp"
#include "list.hpp"
#include "thread.hpp"

#ifndef LIBXR_TIMER_PRIORITY
#define LIBXR_TIMER_PRIORITY Thread::Priority::MEDIUM
#endif

namespace LibXR {
class Timer {
 public:
  class ControlBlock {
   public:
    void Run() { fun_(handle); }

    void (*fun_)(void *);
    void *handle;
    uint32_t cycle_;
    uint32_t count_;
    bool enable_;
  };

  typedef LibXR::List::Node<ControlBlock> *TimerHandle;

  template <typename ArgType>
  static TimerHandle CreatetTask(void (*fun)(ArgType), ArgType arg,
                                 uint32_t cycle) {
    ASSERT(cycle > 0);

    typedef struct {
      LibXR::List::Node<ControlBlock> ctrl_block;
      ArgType arg;
      void (*fun)(ArgType);
    } Data;

    Data *data = new Data;
    data->fun = fun;
    data->arg = arg;

    data->ctrl_block.data_.handle = data;
    data->ctrl_block.data_.fun_ = [](void *arg) {
      Data *data = reinterpret_cast<Data *>(arg);
      data->fun(data->arg);
    };
    data->ctrl_block.data_.count_ = 0;
    data->ctrl_block.data_.cycle_ = cycle;
    data->ctrl_block.data_.enable_ = false;

    return &data->ctrl_block;
  }

  static void Start(TimerHandle handle) { handle->data_.enable_ = true; }

  static void Stop(TimerHandle handle) { handle->data_.enable_ = false; }

  static void SetCycle(TimerHandle handle, uint32_t cycle) {
    handle->data_.cycle_ = cycle;
  }

  static void RefreshThreadFunction(void *) {
    TimestampMS time = Thread::GetTime();
    while (true) {
      Timer::Refresh();
      Thread::SleepUntil(time, 1);
    }
  }

  static void Remove(TimerHandle handle) {
    ASSERT(handle->next_);

    list_->Delete(*handle);
  }

  static void Add(TimerHandle handle) {
    ASSERT(!handle->next_);

    if (!LibXR::Timer::list_) {
      LibXR::Timer::list_ = new LibXR::List();
#ifdef LIBXR_NOT_SUPPORT_MUTI_THREAD
#else
      auto thread_handle = Thread();
      thread_handle.Create<void *>(nullptr, RefreshThreadFunction,
                                   "libxr_timer_task", 512,
                                   LIBXR_TIMER_PRIORITY);
#endif
    }
    list_->Add(*handle);
  }

  static void Refresh() {
    if (!LibXR::Timer::list_) {
      LibXR::Timer::list_ = new LibXR::List();

      auto thread_handle = Thread();
      thread_handle.Create<void *>(nullptr, RefreshThreadFunction,
                                   "libxr_timer_task", 512,
                                   Thread::Priority::HIGH);
    }

    auto fun = [](ControlBlock &block, void *) {
      if (!block.enable_) {
        return ErrorCode::OK;
      }

      block.count_++;

      if (block.count_ >= block.cycle_) {
        block.count_ = 0;
        block.Run();
      }

      return ErrorCode::OK;
    };

    static void *empty = nullptr;

    list_->Foreach<ControlBlock, void *>(fun, std::forward<void *>(empty));
  }

  static void RefreshTimerInIdle();

  static LibXR::List *list_;
};

}  // namespace LibXR
