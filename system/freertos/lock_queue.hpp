#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR
{
/**
 * @brief  线程安全的队列实现，基于 FreeRTOS 消息队列
 *         Thread-safe queue implementation based on FreeRTOS message queue
 * @tparam Data 队列存储的数据类型 The type of data stored in the queue
 *
 * @details
 * 该类封装了 FreeRTOS 的 `xQueue`，提供线程安全的 Push、Pop、Peek 操作，
 * 并支持中断服务例程（ISR）中的数据操作，确保任务间的数据同步与通信。
 *
 * This class wraps FreeRTOS's `xQueue`, providing thread-safe Push, Pop,
 * and Peek operations. It also supports operations in Interrupt Service
 * Routines (ISR) to ensure safe data synchronization and communication
 * between tasks.
 */
template <typename Data>
class LockQueue
{
 public:
  /**
   * @brief  构造函数，创建指定长度的队列
   *         Constructor that creates a queue of specified length
   * @param  length 队列的最大长度 The maximum length of the queue
   */
  LockQueue(size_t length)
      : queue_handle_(xQueueCreate(length, sizeof(Data))), LENGTH(length)
  {
  }

  /**
   * @brief  析构函数，删除队列
   *         Destructor that deletes the queue
   */
  ~LockQueue() { vQueueDelete(queue_handle_); }

  /**
   * @brief  将数据推入队列（非阻塞）
   *         Pushes data into the queue (non-blocking)
   * @param  data 需要推入的数据 The data to be pushed
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Push(const Data &data)
  {
    auto ans = xQueueSend(queue_handle_, &data, 0);
    if (ans == pdTRUE)
    {
      return ErrorCode::OK;
    }
    else
    {
      return ErrorCode::FULL;
    }
  }

  /**
   * @brief  从队列弹出数据（阻塞）
   *         Pops data from the queue (blocking)
   * @param  data 存储弹出数据的变量 Variable to store the popped data
   * @param  timeout 超时时间（默认值：无限等待） Timeout period (default: infinite wait)
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(Data &data, uint32_t timeout = UINT32_MAX)
  {
    auto ans = xQueueReceive(queue_handle_, &data, timeout);
    if (ans == pdTRUE)
    {
      return ErrorCode::OK;
    }
    else
    {
      return ErrorCode::EMPTY;
    }
  }

  /**
   * @brief  从队列弹出数据（不关心数据值）
   *         Pops data from the queue without retrieving its value
   * @param  timeout 超时时间 Timeout period
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(uint32_t timeout)
  {
    Data data;
    auto ans = xQueueReceive(queue_handle_, &data, timeout);
    if (ans == pdTRUE)
    {
      return ErrorCode::OK;
    }
    else
    {
      return ErrorCode::EMPTY;
    }
  }

  /**
   * @brief  从 ISR（中断服务例程）推入数据
   *         Pushes data into the queue from an ISR (Interrupt Service Routine)
   * @param  data 需要推入的数据 The data to be pushed
   * @param  in_isr 是否在 ISR 环境中 Whether it is being called from an ISR
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode PushFromCallback(const Data &data, bool in_isr)
  {
    if (!in_isr)
    {
      return Push(data) == pdTRUE ? ErrorCode::OK : ErrorCode::FULL;
    }
    else
    {
      BaseType_t xHigherPriorityTaskWoken;  // NOLINT
      auto ans = xQueueSendFromISR(queue_handle_, &data, &xHigherPriorityTaskWoken);
      if (xHigherPriorityTaskWoken)
      {
        portYIELD();  // NOLINT
      }
      if (ans == pdTRUE)
      {
        return ErrorCode::OK;
      }
      else
      {
        return ErrorCode::FULL;
      }
    }
  }

  /**
   * @brief  获取队列中的数据项数量
   *         Gets the number of items in the queue
   * @return 当前队列大小 Current queue size
   */
  size_t Size() { return uxQueueMessagesWaiting(queue_handle_); }

  /**
   * @brief  获取队列剩余的可用空间
   *         Gets the remaining available space in the queue
   * @return 可用空间大小 Available space size
   */
  size_t EmptySize() { return uxQueueSpacesAvailable(queue_handle_); }

 private:
  QueueHandle_t queue_handle_;  ///< FreeRTOS 队列句柄 FreeRTOS queue handle
  const uint32_t LENGTH;        ///< 队列最大长度 Maximum queue length
};
}  // namespace LibXR
