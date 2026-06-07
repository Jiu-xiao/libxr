#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR
{
/**
 * @class QueueBase
 * @brief 字节 FIFO 队列，提供固定大小的循环缓冲区
 *        (Byte FIFO queue providing a fixed-size circular buffer).
 *
 * 该类只按固定 element size 搬运字节，不管理强类型对象生命周期。
 * This class moves fixed-size byte elements only and does not manage typed
 * object lifetime.
 */
class QueueBase
{
 public:
  /**
   * @brief 构造函数，初始化队列 (Constructor to initialize the queue).
   * @param element_size 队列中每个元素的大小 (Size of each element in the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   * @param buffer 指向缓冲区的指针 (Pointer to the buffer).
   */
  QueueBase(uint16_t element_size, size_t length, uint8_t* buffer);

  /**
   * @brief 构造函数，初始化队列 (Constructor to initialize the queue).
   * @param element_size 队列中每个元素的大小 (Size of each element in the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  QueueBase(uint16_t element_size, size_t length);

  /**
   * @brief 析构函数，释放队列内存 (Destructor to free queue memory).
   */
  ~QueueBase();

  /**
   * @brief 访问指定索引的元素 (Access an element at a specified index).
   * @param index 目标索引 (Target index).
   * @return 指向该索引处元素的指针 (Pointer to the element at the specified index).
   */
  [[nodiscard]] void* operator[](uint32_t index);

  /**
   * @brief 向队列中添加一个元素 (Push an element into the queue).
   * @param data 指向要添加的数据 (Pointer to the data to be added).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列满时返回 `ErrorCode::FULL`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::FULL` if full).
   */
  ErrorCode PushBytes(const void* data);

  /**
   * @brief 获取队列头部的元素但不移除 (Peek at the front element without removing it).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the data is stored).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PeekBytes(void* data);

  /**
   * @brief 移除队列头部的元素 (Pop the front element from the queue).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the removed data is
   * stored).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PopBytes(void* data = nullptr);

  /**
   * @brief 获取队列中最后一个元素的索引 (Get the index of the last element in the queue).
   * @return 如果队列非空，返回最后一个元素的索引，否则返回 -1
   *         (Returns the index of the last element if the queue is not empty, otherwise
   * -1).
   */
  int GetLastElementIndex() const;

  /**
   * @brief 获取队列中第一个元素的索引 (Get the index of the first element in the queue).
   * @return 如果队列非空，返回第一个元素的索引，否则返回 -1
   *         (Returns the index of the first element if the queue is not empty, otherwise
   * -1).
   */
  int GetFirstElementIndex() const;

  /**
   * @brief 批量推入多个元素 (Push multiple elements into the queue).
   * @param data 指向数据缓冲区 (Pointer to the data buffer).
   * @param size 要推入的元素个数 (Number of elements to push).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列满时返回 `ErrorCode::FULL`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::FULL` if full).
   */
  ErrorCode PushBatchBytes(const void* data, size_t size);

  /**
   * @brief 批量移除多个元素 (Pop multiple elements from the queue).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the removed data is
   * stored).
   * @param size 要移除的元素个数 (Number of elements to remove).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PopBatchBytes(void* data, size_t size);

  /**
   * @brief 批量获取多个元素但不移除 (Peek at multiple elements without removing them).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the data is stored).
   * @param size 要获取的元素个数 (Number of elements to retrieve).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PeekBatchBytes(void* data, size_t size);

  /**
   * @brief 覆盖队列中的数据 (Overwrite the queue with new data).
   * @param data 指向新数据的缓冲区 (Pointer to the new data buffer).
   * @return 操作结果，成功返回 `ErrorCode::OK`
   *         (Operation result: `ErrorCode::OK` on success).
   */
  ErrorCode OverwriteBytes(const void* data);

  /**
   * @brief 重置队列，清空所有数据 (Reset the queue and clear all data).
   */
  void Reset();

  /**
   * @brief 获取队列中的元素个数 (Get the number of elements in the queue).
   * @return 当前队列中的元素个数 (Current number of elements in the queue).
   */
  [[nodiscard]] size_t Size() const;

  /**
   * @brief 获取队列的空闲空间 (Get the available space in the queue).
   * @return 队列中可存储的元素个数 (Number of available slots in the queue).
   */
  [[nodiscard]] size_t EmptySize() const;

  /**
   * @brief 获取队列的最大容量 (Get the maximum capacity of the queue).
   * @return 队列的最大容量 (Maximum capacity of the queue).
   */
  [[nodiscard]] size_t MaxSize() const { return length_; }

  QueueBase(const QueueBase&) = delete;
  QueueBase& operator=(const QueueBase&) = delete;
  QueueBase& operator=(QueueBase&) = delete;
  QueueBase& operator=(const QueueBase&&) = delete;
  QueueBase& operator=(QueueBase&&) = delete;

  uint8_t* queue_array_;        ///< 存储队列数据的数组 (Array storing queue data).
  const uint16_t ELEMENT_SIZE;  ///< 每个元素的大小 (Size of each element).
  size_t head_ = 0;             ///< 头部索引 (Head index).
  size_t tail_ = 0;             ///< 尾部索引 (Tail index).
  bool is_full_ = false;        ///< 队列是否已满 (Indicates if the queue is full).
  size_t length_;               ///< 队列最大容量 (Maximum queue capacity).
  bool own_buffer_ = false;     ///< 是否由队列自己管理缓冲区 (Owns buffer memory).
};
}  // namespace LibXR
