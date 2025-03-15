#pragma once

#include <utility>

#include "libxr_def.hpp"
#include "list.hpp"
#include "thread.hpp"

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

  static void Add(TimerHandle handle);

  static void Refresh() {
    if (!LibXR::Timer::list_) {
      LibXR::Timer::list_ = new LibXR::List();

      auto thread_handle = Thread();
      thread_handle.Create<void *>(nullptr, RefreshThreadFunction,
                                   "libxr_timer_task", 512,
                                   Thread::Priority::HIGH);
    }

    auto fun = [](ControlBlock &block) {
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

    list_->Foreach<ControlBlock>(fun);
  }

  static void RefreshTimerInIdle();

  static LibXR::List *list_;

  static Thread thread_handle_;

  static LibXR::Thread::Priority priority;
  static uint32_t stack_depth;
};

}  // namespace LibXR
