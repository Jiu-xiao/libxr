#pragma once

#include "libxr_system.hpp"
#include "libxr_time.hpp"

namespace LibXR
{

/**
 * @brief  线程管理类，封装 POSIX 线程创建和调度
 *         Thread management class encapsulating POSIX thread creation and scheduling
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
    IDLE,      ///< 空闲优先级 Idle priority
    LOW,       ///< 低优先级 Low priority
    MEDIUM,    ///< 中等优先级 Medium priority
    HIGH,      ///< 高优先级 High priority
    REALTIME,  ///< 实时优先级 Realtime priority
    NUMBER,    ///< 优先级数量 Number of priority levels
  };

  /**
   * @brief  默认构造函数，初始化空线程
   *         Default constructor initializing an empty thread
   */
  Thread() {};

  /**
   * @brief  通过 POSIX 线程句柄创建线程对象
   *         Constructor to create a thread object from a POSIX thread handle
   * @param  handle POSIX 线程句柄 POSIX thread handle
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
   * 该方法基于 POSIX `pthread_create()` 创建新线程，执行 `function` 并传递 `arg`
   * 作为参数。 线程栈大小 `stack_depth` 需要进行调整以符合 POSIX 线程的栈管理方式。
   * 如果系统支持 `SCHED_RR` 调度策略，则设置线程优先级。
   *
   * This method creates a new thread using POSIX `pthread_create()`, executing `function`
   * with `arg` as the argument. The `stack_depth` needs adjustment for POSIX thread stack
   * management. If the system supports `SCHED_RR` scheduling, thread priority is set
   * accordingly.
   */
  template <typename ArgType>
  void Create(ArgType arg, void (*function)(ArgType arg), const char *name,
              size_t stack_depth, Thread::Priority priority)
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // 线程栈大小设定，确保不小于 1
    if (stack_depth > 256)
    {
      pthread_attr_setstacksize(&attr, stack_depth / 1024 * 4);
    }
    else
    {
      pthread_attr_setstacksize(&attr, 1);
    }

    /**
     * @brief  线程数据封装类
     *         Thread data encapsulation class
     */
    class ThreadBlock
    {
     public:
      /**
       * @brief  构造函数，存储线程相关数据
       *         Constructor to store thread-related data
       * @param  fun 线程执行的函数 Function executed by the thread
       * @param  arg 线程参数 Thread argument
       * @param  name 线程名称 Thread name
       */
      ThreadBlock(typeof(function) fun, ArgType arg, const char *name)
          : fun_(fun),
            arg_(arg),
            name_(reinterpret_cast<char *>(malloc(strlen(name) + 1)))
      {
        strcpy(name_, name);
      }

      /**
       * @brief  线程入口函数，执行用户定义的线程函数
       *         Thread entry function that executes the user-defined function
       * @param  arg 线程参数 Thread argument
       * @return 返回值始终为 `nullptr`
       *         The return value is always `nullptr`
       */
      static void *Port(void *arg)
      {
        ThreadBlock *block = static_cast<ThreadBlock *>(arg);
        volatile const char *thread_name = block->name_;
        block->fun_(block->arg_);

        UNUSED(thread_name);
        return static_cast<void *>(nullptr);
      }

      typeof(function) fun_;  ///< 线程执行的函数 Function executed by the thread
      ArgType arg_;           ///< 线程函数的参数 Argument passed to the thread function
      char *name_;            ///< 线程名称 Thread name
    };

    auto block = new ThreadBlock(function, arg, name);

    pthread_create(&this->thread_handle_, &attr, block->Port, block);

    // 设定线程优先级（如果支持 `SCHED_RR`）
    if (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR) >=
        static_cast<int>(Priority::REALTIME))
    {
      struct sched_param sp;
      memset(&sp, 0, sizeof(sp));
      sp.sched_priority = sched_get_priority_min(SCHED_RR) + static_cast<int>(priority);
      pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
    }
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
   * @brief  线程对象转换为 POSIX 线程句柄
   *         Converts the thread object to a POSIX thread handle
   * @return POSIX 线程句柄 POSIX thread handle
   */
  operator libxr_thread_handle() { return thread_handle_; }

 private:
  libxr_thread_handle thread_handle_;  ///< POSIX 线程句柄 POSIX thread handle
};

}  // namespace LibXR
