#pragma once

#include "libxr_system.hpp"
#include "libxr_time.hpp"

#define LIBXR_PRIORITY_STEP ((configMAX_PRIORITIES - 1) / 5)

namespace LibXR
{
/**
 * @brief  线程管理类，封装 FreeRTOS 任务创建和调度
 *         Thread management class encapsulating FreeRTOS task creation and scheduling
 */
class Thread
{
 public:
  /**
   * @brief  线程优先级枚举
   *         Enumeration for thread priorities
   */
  enum class Priority : uint8_t
  {
    IDLE = 0,                            ///< 空闲优先级 Idle priority
    LOW = LIBXR_PRIORITY_STEP * 1,       ///< 低优先级 Low priority
    MEDIUM = LIBXR_PRIORITY_STEP * 2,    ///< 中等优先级 Medium priority
    HIGH = LIBXR_PRIORITY_STEP * 3,      ///< 高优先级 High priority
    REALTIME = LIBXR_PRIORITY_STEP * 4,  ///< 实时优先级 Realtime priority
    NUMBER = 5                           ///< 优先级数量 Number of priority levels
  };

  /**
   * @brief  默认构造函数，初始化空线程
   *         Default constructor initializing an empty thread
   */
  Thread() {};

  /**
   * @brief  通过 FreeRTOS 线程句柄创建线程对象
   *         Constructor to create a thread object from a FreeRTOS thread handle
   * @param  handle FreeRTOS 线程句柄 FreeRTOS thread handle
   */
  Thread(libxr_thread_handle handle) : thread_handle_(handle) {};

  /**
   * @brief  创建新线程
   *         Creates a new thread
   * @tparam ArgType 线程函数的参数类型 The type of argument for the thread function
   * @param  arg 线程函数的参数 Argument for the thread function
   * @param  function 线程执行的函数 Function executed by the thread
   * @param  name 线程名称 Thread name
   * @param  stack_depth 线程栈大小（字节） Stack size of the thread (bytes)
   * @param  priority 线程优先级 Thread priority
   *
   * @details
   * 该方法基于 FreeRTOS `xTaskCreate()` 创建新线程，执行 `function` 并传递 `arg`
   * 作为参数。 线程优先级 `priority` 必须符合 FreeRTOS 配置的 `configMAX_PRIORITIES`
   * 约束。
   *
   * This method creates a new thread using FreeRTOS `xTaskCreate()`, executing `function`
   * with `arg` as the argument. The thread priority `priority` must adhere to FreeRTOS
   * configuration constraints defined by `configMAX_PRIORITIES`.
   */
  template <typename ArgType>
  void Create(ArgType arg, void (*function)(ArgType arg), const char *name,
              size_t stack_depth, Thread::Priority priority)
  {
    ASSERT(configMAX_PRIORITIES >= 6);

    class ThreadBlock
    {
     public:
      ThreadBlock(decltype(function) fun, ArgType arg) : fun_(fun), arg_(arg) {}

      static void Port(void *arg)
      {
        ThreadBlock *block = static_cast<ThreadBlock *>(arg);
        block->fun_(block->arg_);
      }

      decltype(function) fun_;
      ArgType arg_;
    };

    auto block = new ThreadBlock(function, arg);

    auto ans = xTaskCreate(block->Port, name, stack_depth, block,
                           static_cast<uint32_t>(priority), &(this->thread_handle_));
    UNUSED(ans);
    UNUSED(block);
    ASSERT(ans == pdPASS);
  }

  /**
   * @brief  获取当前线程对象
   *         Gets the current thread object
   * @return 当前线程对象 The current thread object
   */
  static Thread Current(void);

  /**
   * @brief  获取当前系统时间（毫秒）
   *         Gets the current system time in milliseconds
   * @return 当前时间（毫秒） Current time in milliseconds
   */
  static uint32_t GetTime();

  /**
   * @brief  让线程进入休眠状态
   *         Puts the thread to sleep
   * @param  milliseconds 休眠时间（毫秒） Sleep duration in milliseconds
   */
  static void Sleep(uint32_t milliseconds);

  /**
   * @brief  让线程休眠直到指定时间点
   *         Puts the thread to sleep until a specified time
   * @param  last_waskup_time 上次唤醒时间 Last wake-up time
   * @param  time_to_sleep 休眠时长（毫秒） Sleep duration in milliseconds
   */
  static void SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep);

  /**
   * @brief  让出 CPU 以执行其他线程
   *         Yields CPU execution to allow other threads to run
   */
  static void Yield();

  /**
   * @brief  线程对象转换为 FreeRTOS 线程句柄
   *         Converts the thread object to a FreeRTOS thread handle
   * @return FreeRTOS 线程句柄 FreeRTOS thread handle
   */
  operator libxr_thread_handle() { return thread_handle_; }

 private:
  libxr_thread_handle thread_handle_;  ///< FreeRTOS 线程句柄 FreeRTOS thread handle
};
}  // namespace LibXR
