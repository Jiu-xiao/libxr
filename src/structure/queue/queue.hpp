#pragma once

#include <cstddef>
#include <cstdint>

#include "queue_base.hpp"
#include "queue_typed_base.hpp"

namespace LibXR
{
/**
 * @brief 基于 QueueBase 的泛型队列模板类
 *        (Generic queue template class based on QueueBase).
 *
 * This class provides a type-safe queue for storing elements of type `Data`.
 * It supports standard queue operations such as push, pop, peek, and batch operations.
 * 该类提供一个类型安全的队列，用于存储 `Data` 类型的元素。
 * 它支持标准的队列操作，如推入（Push）、弹出（Pop）、查看（Peek）以及批量操作。
 *
 * @tparam Data 队列存储的数据类型 (Type of data stored in the queue).
 */
template <typename Data>
class Queue final : public QueueTypedBase<Queue<Data>, Data>, public QueueBase
{
 public:
  using ValueType = Data;  ///< 队列元素类型 / Queue element type.
  using QueueTypedBase<Queue<Data>, Data>::Pop;
  using QueueTypedBase<Queue<Data>, Data>::Push;

  /**
   * @brief 构造函数，初始化队列
   *        (Constructor to initialize the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   */
  explicit Queue(size_t length) : QueueBase(sizeof(Data), length) {}

  /**
   * @brief 构造函数，初始化队列
   *        (Constructor to initialize the queue).
   * @param length 队列的最大容量 (Maximum capacity of the queue).
   * @param buffer 指向缓冲区的指针 (Pointer to the buffer).
   */
  Queue(size_t length, uint8_t* buffer) : QueueBase(sizeof(Data), length, buffer) {}

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
  Data& operator[](int32_t index)
  {
    if (index >= 0)
    {
      index = (head_ + index) % length_;
    }
    else
    {
      index = (tail_ + index + length_) % length_;
    }

    return *reinterpret_cast<Data*>(&queue_array_[index * ELEMENT_SIZE]);
  }

  /**
   * @brief 查看队列头部的元素但不移除
   *        (Peek at the front element without removing it).
   * @param data 用于存储查看到的元素的引用 (Reference to store the peeked element).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   */
  ErrorCode Peek(Data& data) { return QueueBase::PeekBytes(&data); }

  /**
   * @brief 批量推入多个元素
   *        (Push multiple elements into the queue).
   * @param data 指向数据数组 (Pointer to the array of elements to push).
   * @param size 要推入的元素个数 (Number of elements to push).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   *         - `ErrorCode::FULL` 表示队列已满 (`ErrorCode::FULL` if the queue is full).
   */
  ErrorCode PushBatch(const Data* data, size_t size)
  {
    return QueueBase::PushBatchBytes(data, size);
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
  ErrorCode PopBatch(Data* data, size_t size)
  {
    return QueueBase::PopBatchBytes(data, size);
  }

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
  ErrorCode PeekBatch(Data* data, size_t size)
  {
    return QueueBase::PeekBatchBytes(data, size);
  }

  /**
   * @brief 覆盖队列中的数据
   *        (Overwrite the queue with new data).
   * @param data 新数据 (New data to overwrite the queue).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功 (`ErrorCode::OK` on success).
   */
  ErrorCode Overwrite(const Data& data) { return QueueBase::OverwriteBytes(&data); }

  /**
   * @brief 获取队列的最大容量
   *        (Get the maximum capacity of the queue).
   * @return 队列的最大容量 (Maximum capacity of the queue).
   */
  [[nodiscard]] size_t MaxSize() const { return QueueBase::MaxSize(); }
};
}  // namespace LibXR
