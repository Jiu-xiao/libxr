#pragma once

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "list.hpp"
#include "thread.hpp"
#include <utility>

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
    Thread::Priority priority;
  };

  typedef LibXR::List::Node<ControlBlock> *TimerHandle;

  template <typename ArgType>
  static TimerHandle CreatetTask(void (*fun)(ArgType), ArgType arg,
                                 uint32_t cycle, Thread::Priority priority) {
    ASSERT(priority < Thread::PRIORITY_NUMBER);
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
    data->ctrl_block.data_.priority = priority;

    return &data->ctrl_block;
  }

  static void Start(TimerHandle handle) { handle->data_.enable_ = true; }

  static void Stop(TimerHandle handle) { handle->data_.enable_ = false; }

  static void SetCycle(TimerHandle handle, uint32_t cycle) {
    handle->data_.cycle_ = cycle;
  }

  static void SetPriority(TimerHandle handle, Thread::Priority priority) {
    ASSERT(priority < Thread::PRIORITY_NUMBER);

    if (handle->next_) {
      Remove(handle);
      handle->data_.priority = priority;
      Add(handle);
    } else {
      handle->data_.priority = priority;
    }
  }

  static void Remove(TimerHandle handle) {
    ASSERT(handle->next_);
    list_[handle->data_.priority]->Delete(*handle);
  }

  static void Add(TimerHandle handle) {
    ASSERT(!handle->next_);
    list_[handle->data_.priority]->Add(*handle);
  }

  static void Refresh(Thread::Priority priority) {
    ErrorCode (*fun)(ControlBlock & block, void *&) = [](ControlBlock &block,
                                                         void *&) {
      if (!block.enable_) {
        return NO_ERR;
      }

      block.count_++;

      if (block.count_ >= block.cycle_) {
        block.count_ = 0;
        block.Run();
      }

      return NO_ERR;
    };

    static void *empty = NULL;

    list_[priority]->Foreach<ControlBlock, void *>(fun, empty);
  }

  static LibXR::List *list_[Thread::PRIORITY_NUMBER];
};

} // namespace LibXR
