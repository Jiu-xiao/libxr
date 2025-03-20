#pragma once

#include <utility>

#include "libxr_def.hpp"
#include "list.hpp"
#include "thread.hpp"

namespace LibXR
{

/**
 * @brief  定时器类，实现周期性任务调度
 *         Timer class for scheduling periodic tasks
 *
 * @details
 * 该定时器支持多任务调度，可用于执行周期性任务，如定时回调函数调用。
 * Timer 提供任务创建、启动、停止、删除及周期调整等功能，并使用 `Thread::SleepUntil`
 * 实现精确调度。
 *
 * This timer supports multi-task scheduling and can be used to execute periodic tasks,
 * such as invoking callback functions at regular intervals.
 * It provides task creation, start, stop, delete, and cycle adjustment functionalities,
 * utilizing `Thread::SleepUntil` for precise scheduling.
 */
class Timer
{
 public:
  /**
   * @brief  控制块类，存储任务信息
   *         Control block class for storing task information
   */
  class ControlBlock
  {
   public:
    /**
     * @brief  运行定时任务
     *         Runs the scheduled task
     */
    void Run() { fun_(handle); }

    void (*fun_)(void *);  ///< 任务执行函数 Function pointer to the task
    void *handle;          ///< 任务句柄 Handle to the task
    uint32_t cycle_;  ///< 任务周期（单位：毫秒） Task cycle (unit: milliseconds)
    uint32_t count_;  ///< 计数器 Counter
    bool enable_;     ///< 任务是否启用 Flag indicating whether the task is enabled
  };

  typedef LibXR::List::Node<ControlBlock>
      *TimerHandle;  ///< 定时器任务句柄 Timer task handle

  /**
   * @brief  创建定时任务
   *         Creates a periodic task
   * @tparam ArgType 任务参数类型 Type of task argument
   * @param  fun 定时执行的任务函数 Function to execute periodically
   * @param  arg 任务参数 Argument for the function
   * @param  cycle 任务周期（毫秒） Task execution cycle (milliseconds)
   * @return 任务句柄 TimerHandle pointing to the created task
   *
   * @details
   * 该方法创建一个新的周期性任务，任务将在 `cycle` 毫秒的周期内运行。
   * 若 `cycle` 小于等于 0，则会触发 `ASSERT` 断言。
   *
   * This method creates a new periodic task that runs in a cycle of `cycle` milliseconds.
   * If `cycle` is less than or equal to 0, it triggers an `ASSERT` assertion.
   */
  template <typename ArgType>
  static TimerHandle CreateTask(void (*fun)(ArgType), ArgType arg, uint32_t cycle)
  {
    ASSERT(cycle > 0);

    typedef struct
    {
      LibXR::List::Node<ControlBlock> ctrl_block;
      ArgType arg;
      void (*fun)(ArgType);
    } Data;

    Data *data = new Data;
    data->fun = fun;
    data->arg = arg;

    data->ctrl_block.data_.handle = data;
    data->ctrl_block.data_.fun_ = [](void *arg)
    {
      Data *data = reinterpret_cast<Data *>(arg);
      data->fun(data->arg);
    };
    data->ctrl_block.data_.count_ = 0;
    data->ctrl_block.data_.cycle_ = cycle;
    data->ctrl_block.data_.enable_ = false;

    return &data->ctrl_block;
  }

  /**
   * @brief  启动定时任务
   *         Starts a periodic task
   * @param  handle 任务句柄 Timer handle to start
   */
  static void Start(TimerHandle handle) { handle->data_.enable_ = true; }

  /**
   * @brief  停止定时任务
   *         Stops a periodic task
   * @param  handle 任务句柄 Timer handle to stop
   */
  static void Stop(TimerHandle handle) { handle->data_.enable_ = false; }

  /**
   * @brief  设置定时任务的周期
   *         Sets the cycle of a periodic task
   * @param  handle 任务句柄 Timer handle to modify
   * @param  cycle 任务周期（毫秒） New cycle time (milliseconds)
   */
  static void SetCycle(TimerHandle handle, uint32_t cycle)
  {
    handle->data_.cycle_ = cycle;
  }

  /**
   * @brief  定时器管理线程函数
   *         Timer management thread function
   * @param  unused 未使用参数 Unused parameter
   *
   * @details
   * 该线程持续运行，定期刷新任务列表，并确保任务按时执行。
   * `Thread::SleepUntil` 方式用于精确调度。
   *
   * This thread runs continuously, periodically refreshing the task list
   * and ensuring timely task execution.
   * `Thread::SleepUntil` is used for precise scheduling.
   */
  static void RefreshThreadFunction(void *unused)
  {
    TimestampMS time = Thread::GetTime();
    while (true)
    {
      Timer::Refresh();
      Thread::SleepUntil(time, 1);
    }
  }

  /**
   * @brief  删除定时任务
   *         Removes a periodic task
   * @param  handle 任务句柄 Timer handle to remove
   *
   * @details
   * 该方法删除已注册的定时任务，并确保任务列表完整性。
   * 若 `handle->next_` 为空，则会触发 `ASSERT` 断言。
   *
   * This method removes a registered periodic task while ensuring
   * the integrity of the task list.
   * If `handle->next_` is null, it triggers an `ASSERT` assertion.
   */
  static void Remove(TimerHandle handle)
  {
    ASSERT(handle->next_);
    list_->Delete(*handle);
  }

  /**
   * @brief  添加定时任务
   *         Adds a periodic task
   * @param  handle 任务句柄 Timer handle to add
   */
  static void Add(TimerHandle handle);

  /**
   * @brief  刷新定时任务状态
   *         Refreshes the state of periodic tasks
   *
   * @details
   * 该方法遍历任务列表，并检查任务是否应当运行。
   * 若任务启用，并且 `count_` 达到 `cycle_`，则执行任务并重置计数器。
   *
   * This method iterates through the task list and checks whether a task should run.
   * If a task is enabled and its `count_` reaches `cycle_`, the task is executed and the
   * counter is reset.
   */
  static void Refresh()
  {
    if (!LibXR::Timer::list_)
    {
      LibXR::Timer::list_ = new LibXR::List();

#ifndef LIBXR_NOT_SUPPORT_MUTI_THREAD

      auto thread_handle = Thread();
      thread_handle.Create<void *>(nullptr, RefreshThreadFunction, "libxr_timer_task",
                                   512, Thread::Priority::HIGH);
#endif
    }

    auto fun = [](ControlBlock &block)
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

  /**
   * @brief  在空闲时刷新定时器
   *         Refreshes the timer during idle time
   */
  static void RefreshTimerInIdle();

  static LibXR::List *list_;  ///< 定时任务列表 List of registered tasks

  static Thread thread_handle_;  ///< 定时器管理线程 Timer management thread

  static LibXR::Thread::Priority priority;  ///< 线程优先级 Thread priority
  static uint32_t stack_depth;              ///< 线程栈深度 Thread stack depth
};

}  // namespace LibXR
