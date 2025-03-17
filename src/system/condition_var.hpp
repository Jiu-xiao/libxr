#pragma once

#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR
{

/**
 * @class ConditionVar
 * @brief 条件变量封装类 / Condition variable wrapper class
 *
 * 该类提供了条件变量 (`Condition Variable`) 的封装，实现了线程间的同步机制，
 * 允许一个线程等待某个条件发生，另一个线程通知该条件已满足。
 * This class provides a wrapper for condition variables, implementing a synchronization
 * mechanism where one thread waits for a condition to occur while another thread signals
 * the condition.
 */
class ConditionVar
{
 private:
  condition_var_handle
      handle_;  ///< 底层条件变量句柄 / Underlying condition variable handle

 public:
  /**
   * @brief 默认构造函数 / Default constructor
   *
   * 初始化条件变量的内部句柄。
   * Initializes the internal handle of the condition variable.
   */
  ConditionVar();

  /**
   * @brief 析构函数 / Destructor
   *
   * 释放条件变量资源。
   * Releases the condition variable resources.
   */
  ~ConditionVar();

  /**
   * @brief 等待条件变量 / Waits for the condition variable
   * @param timeout 等待的超时时间（毫秒） / Timeout in milliseconds
   * @return 操作结果，成功返回 `ErrorCode::OK`，超时返回 `ErrorCode::TIMEOUT` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::TIMEOUT` if
   * timed out
   *
   * 该方法会阻塞当前线程，直到收到信号或超时。
   * This method blocks the current thread until it receives a signal or the timeout
   * occurs.
   */
  ErrorCode Wait(uint32_t timeout);

  /**
   * @brief 发送信号唤醒一个等待线程 / Signals one waiting thread
   *
   * 该方法通知一个正在等待条件变量的线程，使其继续执行。
   * This method notifies a single thread waiting on the condition variable to resume
   * execution.
   */
  void Signal();

  /**
   * @brief 广播信号唤醒所有等待线程 / Broadcasts a signal to wake up all waiting threads
   *
   * 该方法通知所有正在等待条件变量的线程，使它们继续执行。
   * This method notifies all threads waiting on the condition variable to resume
   * execution.
   */
  void Broadcast();
};

}  // namespace LibXR
