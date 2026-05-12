#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include "mutex.hpp"
#include "queue.hpp"
#include "semaphore.hpp"

namespace LibXR
{

/**
 * @brief  线程安全的锁队列类，提供同步和异步操作支持
 *         Thread-safe lock queue class with synchronous and asynchronous operation
 * support
 * @tparam Data 队列存储的数据类型 The type of data stored in the queue
 */
template <typename Data>
class LockQueue
{
 public:
  /**
   * @brief  构造函数，初始化队列
   *         Constructor to initialize the queue
   * @param  length 队列的最大长度 Maximum length of the queue
   */
  LockQueue(size_t length) : queue_handle_(length) {}

  /**
   * @brief  析构函数，释放资源
   *         Destructor to release resources
   */
  ~LockQueue() {}

  /**
   * @brief  向队列中推送数据
   *         Pushes data into the queue
   * @param  data 要推送的数据 The data to be pushed
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Push(const Data &data)
  {
    mutex_.Lock();
    auto ans = queue_handle_.Push(data);
    if (ans == ErrorCode::OK)
    {
      semaphore_handle_.Post();
    }
    mutex_.Unlock();
    return ans;
  }

  /**
   * @brief  从队列中弹出数据（带超时）
   *         Pops data from the queue with timeout
   * @param  data 存储弹出数据的变量 Variable to store the popped data
   * @param  timeout 超时时间（毫秒） Timeout in milliseconds
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(Data &data, uint32_t timeout)
  {
    if (semaphore_handle_.Wait(timeout) == ErrorCode::OK)
    {
      mutex_.Lock();
      auto ans = queue_handle_.Pop(data);
      mutex_.Unlock();
      return ans;
    }
    else
    {
      return ErrorCode::TIMEOUT;
    }
  }

  /**
   * @brief  无参数弹出数据
   *         Pops data from the queue without storing it
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop()
  {
    mutex_.Lock();
    auto ans = queue_handle_.Pop();
    mutex_.Unlock();
    return ans;
  }

  /**
   * @brief  带超时的弹出数据
   *         Pops data from the queue with timeout
   * @param  timeout 超时时间（毫秒） Timeout in milliseconds
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(uint32_t timeout)
  {
    if (semaphore_handle_.Wait(timeout) == ErrorCode::OK)
    {
      mutex_.Lock();
      auto ans = queue_handle_.Pop();
      mutex_.Unlock();
      return ans;
    }
    else
    {
      return ErrorCode::TIMEOUT;
    }
  }

  /**
   * @brief  从回调函数中推送数据
   *         Pushes data into the queue from a callback function
   * @param  data 要推送的数据 The data to be pushed
   * @param  in_isr 是否在中断上下文中 Whether the function is called from an ISR
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode PushFromCallback(const Data &data, bool in_isr)
  {
    UNUSED(in_isr);
    return Push(data);
  }

  /**
   * @brief  获取队列中的数据项数量
   *         Gets the number of items in the queue
   * @return 当前队列大小 Current queue size
   */
  size_t Size()
  {
    mutex_.Lock();
    auto ans = queue_handle_.Size();
    mutex_.Unlock();
    return ans;
  }

  /**
   * @brief  获取队列的剩余容量
   *         Gets the remaining capacity of the queue
   * @return 剩余空间大小 Remaining space size
   */
  size_t EmptySize()
  {
    mutex_.Lock();
    auto ans = queue_handle_.EmptySize();
    mutex_.Unlock();
    return ans;
  }

 private:
  Queue<Data> queue_handle_;    ///< 底层队列对象 Underlying queue object
  Mutex mutex_;                 ///< 互斥锁 Mutex for thread safety
  Semaphore semaphore_handle_;  ///< 信号量 Semaphore for synchronization
};

}  // namespace LibXR
