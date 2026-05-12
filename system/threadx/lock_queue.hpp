#pragma once

#include <cstdint>
#include <cstring>

#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "tx_api.h"

namespace LibXR
{
/**
 * @brief  线程安全的队列实现，基于 ThreadX 消息队列
 *         Thread-safe queue implementation based on ThreadX message queue
 * @tparam Data 队列存储的数据类型 The type of data stored in the queue
 *
 * @details
 * 该类封装了 ThreadX 的 `TX_QUEUE`，提供线程安全的 Push、Pop、Peek 操作，
 * 并支持中断服务例程（ISR）中的数据操作，确保任务间的数据同步与通信。
 *
 * This class wraps ThreadX's `TX_QUEUE`, providing thread-safe Push, Pop,
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
  LockQueue(size_t length) : LENGTH(length)
  {
    static_assert(sizeof(Data) % sizeof(ULONG) == 0,
                  "Data must align to ULONG for ThreadX queue");

    // 分配静态内存，单位是 ULONG，大小为 message size * length
    queue_buffer_ = new ULONG[length * (sizeof(Data) / sizeof(ULONG))];

    tx_queue_create(&queue_handle_, const_cast<char *>("xr_queue"),
                    sizeof(Data) / sizeof(ULONG), queue_buffer_, length * sizeof(Data));
  }

  /**
   * @brief  析构函数，删除队列
   *         Destructor that deletes the queue
   */
  ~LockQueue()
  {
    tx_queue_delete(&queue_handle_);
    delete[] queue_buffer_;
  }

  /**
   * @brief  将数据推入队列（非阻塞）
   *         Pushes data into the queue (non-blocking)
   * @param  data 需要推入的数据 The data to be pushed
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Push(const Data &data)
  {
    UINT status = tx_queue_send(&queue_handle_, (void *)&data, TX_NO_WAIT);
    return (status == TX_SUCCESS) ? ErrorCode::OK : ErrorCode::FULL;
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
    ULONG tx_timeout = (timeout == UINT32_MAX)
                           ? TX_WAIT_FOREVER
                           : timeout * TX_TIMER_TICKS_PER_SECOND / 1000;
    if (tx_timeout == 0 && timeout > 0) tx_timeout = 1;

    UINT status = tx_queue_receive(&queue_handle_, (void *)&data, tx_timeout);
    return (status == TX_SUCCESS) ? ErrorCode::OK : ErrorCode::EMPTY;
  }

  /**
   * @brief  从队列弹出数据（不关心数据值）
   *         Pops data from the queue without retrieving its value
   * @param  timeout 超时时间 Timeout period
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(uint32_t timeout)
  {
    Data dummy;
    return Pop(dummy, timeout);
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
    // ThreadX 不区分 ISR/线程上下文，直接调用即可
    UNUSED(in_isr);
    UINT status = tx_queue_send(&queue_handle_, (void *)&data, TX_NO_WAIT);
    return (status == TX_SUCCESS) ? ErrorCode::OK : ErrorCode::FULL;
  }

  /**
   * @brief  获取队列中的数据项数量
   *         Gets the number of items in the queue
   * @return 当前队列大小 Current queue size
   */
  size_t Size()
  {
    ULONG enqueued;
    tx_queue_info_get(&queue_handle_, NULL, &enqueued, NULL, NULL, NULL, NULL);
    return static_cast<size_t>(enqueued);
  }

  /**
   * @brief  获取队列剩余的可用空间
   *         Gets the remaining available space in the queue
   * @return 可用空间大小 Available space size
   */
  size_t EmptySize()
  {
    ULONG available;
    tx_queue_info_get(&queue_handle_, NULL, NULL, &available, NULL, NULL, NULL);
    return static_cast<size_t>(available);
  }

 private:
  TX_QUEUE queue_handle_;          ///< ThreadX 队列句柄 ThreadX queue handle
  ULONG *queue_buffer_ = nullptr;  ///< 队列存储区 Queue storage buffer
  const size_t LENGTH;             ///< 队列最大长度 Maximum queue length
};

}  // namespace LibXR
