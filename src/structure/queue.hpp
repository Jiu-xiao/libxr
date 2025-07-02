#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "libxr_def.hpp"

namespace LibXR
{
/**
 * @brief 基础队列类，提供固定大小的循环缓冲区
 *        (Base queue class providing a fixed-size circular buffer).
 *
 * This class implements a circular queue that supports element insertion,
 * retrieval, and batch operations.
 * 该类实现了一个循环队列，支持元素插入、读取和批量操作。
 */
class BaseQueue
{
 public:
  /**
   * @brief 构造函数，初始化队列 (Constructor to initialize the queue).
   * @param element_size 队列中每个元素的大小 (Size of each element in the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   * @param buffer 指向缓冲区的指针 (Pointer to the buffer).
   */
  BaseQueue(uint16_t element_size, size_t length, uint8_t *buffer);

  /**
   * @brief 构造函数，初始化队列 (Constructor to initialize the queue).
   * @param element_size 队列中每个元素的大小 (Size of each element in the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   */
  BaseQueue(uint16_t element_size, size_t length);

  /**
   * @brief 析构函数，释放队列内存 (Destructor to free queue memory).
   */
  ~BaseQueue();

  /**
   * @brief 访问指定索引的元素 (Access an element at a specified index).
   * @param index 目标索引 (Target index).
   * @return 指向该索引处元素的指针 (Pointer to the element at the specified index).
   */
  [[nodiscard]] void *operator[](uint32_t index);

  /**
   * @brief 向队列中添加一个元素 (Push an element into the queue).
   * @param data 指向要添加的数据 (Pointer to the data to be added).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列满时返回 `ErrorCode::FULL`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::FULL` if full).
   */
  ErrorCode Push(const void *data);

  /**
   * @brief 获取队列头部的元素但不移除 (Peek at the front element without removing it).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the data is stored).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode Peek(void *data);

  /**
   * @brief 移除队列头部的元素 (Pop the front element from the queue).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the removed data is
   * stored).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode Pop(void *data = nullptr);

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
  ErrorCode PushBatch(const void *data, size_t size);

  /**
   * @brief 批量移除多个元素 (Pop multiple elements from the queue).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the removed data is
   * stored).
   * @param size 要移除的元素个数 (Number of elements to remove).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PopBatch(void *data, size_t size);

  /**
   * @brief 批量获取多个元素但不移除 (Peek at multiple elements without removing them).
   * @param data 指向存储数据的缓冲区 (Pointer to buffer where the data is stored).
   * @param size 要获取的元素个数 (Number of elements to retrieve).
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空时返回 `ErrorCode::EMPTY`
   *         (Operation result: `ErrorCode::OK` on success, `ErrorCode::EMPTY` if empty).
   */
  ErrorCode PeekBatch(void *data, size_t size);

  /**
   * @brief 覆盖队列中的数据 (Overwrite the queue with new data).
   * @param data 指向新数据的缓冲区 (Pointer to the new data buffer).
   * @return 操作结果，成功返回 `ErrorCode::OK`
   *         (Operation result: `ErrorCode::OK` on success).
   */
  ErrorCode Overwrite(const void *data);

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

  BaseQueue(const BaseQueue &) = delete;
  BaseQueue &operator=(const BaseQueue &) = delete;
  BaseQueue &operator=(BaseQueue &) = delete;
  BaseQueue &operator=(const BaseQueue &&) = delete;
  BaseQueue &operator=(BaseQueue &&) = delete;

  uint8_t *queue_array_;        ///< 存储队列数据的数组 (Array storing queue data).
  const uint16_t ELEMENT_SIZE;  ///< 每个元素的大小 (Size of each element).
  size_t head_ = 0;             ///< 头部索引 (Head index).
  size_t tail_ = 0;             ///< 尾部索引 (Tail index).
  bool is_full_ = false;        ///< 队列是否已满 (Indicates if the queue is full).
  size_t length_;               ///< 队列最大容量 (Maximum queue capacity).
  bool own_buffer_ = false;     ///< 是否由队列自己管理缓冲区 (Owns buffer memory).
};

/**
 * @brief 基于 BaseQueue 的泛型队列模板类
 *        (Generic queue template class based on BaseQueue).
 *
 * This class provides a type-safe queue for storing elements of type `Data`.
 * It supports standard queue operations such as push, pop, peek, and batch operations.
 * 该类提供一个类型安全的队列，用于存储 `Data` 类型的元素。
 * 它支持标准的队列操作，如推入（Push）、弹出（Pop）、查看（Peek）以及批量操作。
 *
 * @tparam Data 队列存储的数据类型 (Type of data stored in the queue).
 */
template <typename Data>
class Queue : public BaseQueue
{
 public:
  /**
   * @brief 构造函数，初始化队列
   *        (Constructor to initialize the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   */
  Queue(size_t length) : BaseQueue(sizeof(Data), length) {}

  /**
   * @brief 构造函数，初始化队列
   *        (Constructor to initialize the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   * @param buffer 指向缓冲区的指针 (Pointer to the buffer).
   */
  Queue(size_t length, uint8_t *buffer) : BaseQueue(sizeof(Data), length, buffer) {}

  /**
   * @brief 访问队列中的元素
   *        (Access an element in the queue).
   *
   * This function provides indexed access to elements in the queue.
   * If the index is positive, it is relative to `head_` (front of the queue).
   * If negative, it is relative to `tail_` (end of the queue).
   * 该函数允许使用索引访问队列中的元素。
   * 如果索引为正，则表示从 `head_`（队列头部）开始的偏移量。
   * 如果索引为负，则表示从 `tail_`（队列尾部）开始的偏移量。
   *
   * @param index 访问的索引值 (Index to access).
   * @return 返回索引对应的元素引用 (Reference to the element at the specified index).
   */
  Data &operator[](int32_t index)
  {
    if (index >= 0)
    {
      index = (head_ + index) % length_;
    }
    else
    {
      index = (tail_ + index + length_) % length_;
    }

    return *reinterpret_cast<Data *>(&queue_array_[index * ELEMENT_SIZE]);
  }

  /**
   * @brief 向队列中添加一个元素
   *        (Push an element into the queue).
   * @param data 要推入的元素 (Element to push).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::FULL` 表示队列已满 (`ErrorCode::FULL` if the queue is full).
   */
  ErrorCode Push(const Data &data) { return BaseQueue::Push(&data); }

  /**
   * @brief 从队列中移除头部元素，并获取该元素的数据
   *        (Remove the front element from the queue and retrieve its data).
   * @param data 用于存储弹出元素的引用 (Reference to store the popped element).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode Pop(Data &data) { return BaseQueue::Pop(&data); }

  /**
   * @brief 仅从队列中移除头部元素，不获取数据
   *        (Remove the front element from the queue without retrieving data).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode Pop() { return BaseQueue::Pop(); }

  /**
   * @brief 查看队列头部的元素但不移除
   *        (Peek at the front element without removing it).
   * @param data 用于存储查看到的元素的引用 (Reference to store the peeked element).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode Peek(Data &data) { return BaseQueue::Peek(&data); }

  /**
   * @brief 批量推入多个元素
   *        (Push multiple elements into the queue).
   * @param data 指向数据数组 (Pointer to the array of elements to push).
   * @param size 要推入的元素个数 (Number of elements to push).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::FULL` 表示队列已满 (`ErrorCode::FULL` if the queue is full).
   */
  ErrorCode PushBatch(const Data *data, size_t size)
  {
    return BaseQueue::PushBatch(data, size);
  }

  /**
   * @brief 批量移除多个元素，并获取数据
   *        (Pop multiple elements from the queue and retrieve data).
   * @param data 指向存储数据的数组 (Pointer to the array where popped elements will be
   * stored).
   * @param size 要移除的元素个数 (Number of elements to remove).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode PopBatch(Data *data, size_t size) { return BaseQueue::PopBatch(data, size); }

  /**
   * @brief 批量查看多个元素但不移除
   *        (Peek at multiple elements without removing them).
   * @param data 指向存储数据的数组 (Pointer to the array where peeked elements will be
   * stored).
   * @param size 要查看的元素个数 (Number of elements to retrieve).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode PeekBatch(Data *data, size_t size)
  {
    return BaseQueue::PeekBatch(data, size);
  }

  /**
   * @brief 覆盖队列中的数据
   *        (Overwrite the queue with new data).
   * @param data 新数据 (New data to overwrite the queue).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   */
  ErrorCode Overwrite(const Data &data) { return BaseQueue::Overwrite(&data); }
};

}  // namespace LibXR
