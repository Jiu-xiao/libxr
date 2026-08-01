#pragma once

#include "libxr_assert.hpp"
#include "libxr_system.hpp"
#include "thread_timestamp.hpp"

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
  void Create(ArgType arg, void (*function)(ArgType arg), const char* name,
              size_t stack_depth, Thread::Priority priority)
  {
    ASSERT(configMAX_PRIORITIES >= 6);

    class ThreadBlock
    {
     public:
      ThreadBlock(decltype(function) fun, ArgType arg) : fun_(fun), arg_(arg) {}

      static void Port(void* arg)
      {
        ThreadBlock* block = static_cast<ThreadBlock*>(arg);
        block->fun_(block->arg_);
        delete block;
      }

      decltype(function) fun_;
      ArgType arg_;
    };

    auto block = new ThreadBlock(function, arg);

    uint32_t stack_size = stack_depth / 4;

    if (stack_depth % 4 != 0)
    {
      stack_size += 1;
    }

    auto ans = xTaskCreate(block->Port, name, stack_size, block,
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
   * @brief 获取当前调度器时间 / Get the current scheduler time
   * @return 调度器时钟域的当前时间戳 / Current scheduler-domain timestamp
   * @note 返回值使用 FreeRTOS 原生 tick，只能作为调度游标；耗时测量应使用 Timebase。
   * The value uses native FreeRTOS ticks and is only a scheduling cursor; use Timebase
   * for elapsed-time measurements. Call `GetTimeFromISR()` from interrupt context.
   */
  static ThreadTimestamp GetTime();

  /**
   * @brief 在中断中读取调度器时间 / Read scheduler time from an interrupt
   * @return 调度器时钟域的当前时间 / Current scheduler-domain timestamp
   * @note 调用方中断必须满足 FreeRTOS 对 `FromISR` API 的优先级约束。
   * The calling interrupt must satisfy the FreeRTOS priority contract for `FromISR`
   * APIs.
   */
  static ThreadTimestamp GetTimeFromISR();

  /**
   * @brief  让线程进入休眠状态
   *         Puts the thread to sleep
   * @param  milliseconds 休眠时间（毫秒） Sleep duration in milliseconds
   */
  static void Sleep(uint32_t milliseconds);

  /**
   * @brief 按固定周期休眠 / Sleep on a fixed scheduler period
   * @param  last_wakeup_time 上次唤醒游标，由 `GetTime()` 初始化 / Previous wake cursor,
   * initialized by `GetTime()`
   * @param  time_to_sleep 休眠时长（毫秒），换算后必须位于调度 tick 的未来半环 / Sleep
   * duration in milliseconds; after conversion it must fit in the future half-ring
   */
  static void SleepUntil(ThreadTimestamp& last_wakeup_time, uint32_t time_to_sleep);

  /**
   * @brief  让出 CPU 以执行其他线程
   *         Yields CPU execution to allow other threads to run
   */
  static void Yield() { portYIELD(); }  // NOLINT

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
