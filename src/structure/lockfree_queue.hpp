#pragma once

#include <atomic>
#include <cstddef>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @class LockFreeQueue
 * @brief 无锁队列实现 / Lock-free queue implementation
 *
 * 该类实现了单生产者多消费者无锁队列（SPMC Lock-Free
 * Queue），支持多线程环境下的高效入队和出队操作，
 * 适用于需要高并发性能的场景，如实时系统和多线程数据处理。
 * This class implements a single-producer, multiple-consumer lock-free queue (SPMC
 * Lock-Free Queue) that supports high-efficiency enqueue and dequeue operations in a
 * multi-threaded environment. It is suitable for scenarios requiring high concurrency,
 * such as real-time systems and multi-threaded data processing.
 *
 * @tparam Data 队列存储的数据类型 / The type of data stored in the queue.
 */
template <typename Data>
class alignas(LIBXR_CACHE_LINE_SIZE) LockFreeQueue
{
  inline constexpr size_t AlignUp(size_t size, size_t align)
  {
    return ((size + align - 1) / align) * align;
  }

 public:
  /**
   * @brief 构造函数 / Constructor
   * @param length 队列的最大容量 / Maximum capacity of the queue
   *
   * 创建一个指定大小的无锁队列，并初始化相关变量。
   * Creates a lock-free queue with the specified size and initializes relevant variables.
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  LockFreeQueue(size_t length)
      : head_(0),
        tail_(0),
        LENGTH(AlignUp(length, LIBXR_ALIGN_SIZE) - 1),
        queue_handle_(new Data[LENGTH + 1])
  {
  }

  /**
   * @brief 析构函数 / Destructor
   *
   * 释放队列所占用的内存。
   * Releases the memory occupied by the queue.
   */
  ~LockFreeQueue() { delete[] queue_handle_; }

  /**
   * @brief 获取指定索引的数据指针 / Retrieves the data pointer at a specified index
   * @param index 数据索引 / Data index
   * @return 指向该索引数据的指针 / Pointer to the data at the given index
   */
  Data *operator[](uint32_t index) { return &queue_handle_[static_cast<size_t>(index)]; }

  /**
   * @brief 向队列中推入数据 / Pushes data into the queue
   * @param item 要插入的元素 / Element to be inserted
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列满返回 `ErrorCode::FULL` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::FULL` if
   * the queue is full
   */
  template <typename ElementData = Data>
  ErrorCode Push(ElementData &&item)
  {
    const auto CURRENT_TAIL = tail_.load(std::memory_order_relaxed);
    const auto NEXT_TAIL = Increment(CURRENT_TAIL);

    if (NEXT_TAIL == head_.load(std::memory_order_acquire))
    {
      return ErrorCode::FULL;
    }

    queue_handle_[CURRENT_TAIL] = std::forward<ElementData>(item);
    tail_.store(NEXT_TAIL, std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 从队列中弹出数据 / Pops data from the queue
   * @param item 用于存储弹出数据的变量 / Variable to store the popped data
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空返回 `ErrorCode::EMPTY` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::EMPTY` if
   * the queue is empty
   */
  template <typename ElementData = Data>
  ErrorCode Pop(ElementData &item)
  {
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true)
    {
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      item = queue_handle_[current_head];

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 从队列中移除头部元素，并获取该元素的数据 (Remove the front element from the
   * queue and retrieve its data).
   *
   * This function atomically updates the `head_` index using `compare_exchange_weak`
   * to ensure thread safety. If the queue is empty, it returns `ErrorCode::EMPTY`.
   * Otherwise, it updates the head pointer, retrieves the element, and returns
   * `ErrorCode::OK`. 该函数使用 `compare_exchange_weak` 原子地更新 `head_`
   * 索引，以确保线程安全。 如果队列为空，则返回
   * `ErrorCode::EMPTY`，否则更新头指针，获取元素数据，并返回 `ErrorCode::OK`。
   *
   * @param item 用于存储弹出元素的引用 (Reference to store the popped element).
   * @return 操作结果 (Operation result):
   *         - `ErrorCode::OK` 表示成功移除并获取元素 (`ErrorCode::OK` if an element was
   * successfully removed and retrieved).
   *         - `ErrorCode::EMPTY` 表示队列为空 (`ErrorCode::EMPTY` if the queue is empty).
   *
   */
  ErrorCode Pop(Data &item)
  {
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true)
    {
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      item = queue_handle_[current_head];

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 从队列中弹出数据（不返回数据） / Pops data from the queue (without returning
   * data)
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空返回 `ErrorCode::EMPTY` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::EMPTY` if
   * the queue is empty
   */
  ErrorCode Pop()
  {
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true)
    {
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 获取队列头部数据但不弹出 / Retrieves the front data of the queue without
   * popping
   * @param item 用于存储获取的数据 / Variable to store the retrieved data
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空返回 `ErrorCode::EMPTY` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::EMPTY` if
   * the queue is empty
   */
  ErrorCode Peek(Data &item)
  {
    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      item = queue_handle_[current_head];

      if (head_.load(std::memory_order_acquire) == current_head)
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 批量推入数据 / Pushes multiple elements into the queue
   * @param data 数据数组指针 / Pointer to the data array
   * @param size 数据个数 / Number of elements
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列满返回 `ErrorCode::FULL` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::FULL` if
   * the queue is full
   */
  ErrorCode PushBatch(const Data *data, size_t size)
  {
    auto current_tail = tail_.load(std::memory_order_relaxed);
    auto current_head = head_.load(std::memory_order_acquire);

    size_t capacity = LENGTH + 1;
    size_t free_space = (current_tail >= current_head)
                            ? (capacity - (current_tail - current_head) - 1)
                            : (current_head - current_tail - 1);

    if (free_space < size)
    {
      return ErrorCode::FULL;
    }

    size_t first_chunk = LibXR::min(size, capacity - current_tail);
    LibXR::Memory::FastCopy(reinterpret_cast<void *>(queue_handle_ + current_tail),
                            reinterpret_cast<const void *>(data),
                            first_chunk * sizeof(Data));

    if (size > first_chunk)
    {
      LibXR::Memory::FastCopy(reinterpret_cast<void *>(queue_handle_),
                              reinterpret_cast<const void *>(data + first_chunk),
                              (size - first_chunk) * sizeof(Data));
    }

    tail_.store((current_tail + size) % capacity, std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 批量弹出数据 / Pops multiple elements from the queue
   * @param data 数据存储数组指针 / Pointer to the array to store popped data
   * @param size 需要弹出的数据个数 / Number of elements to pop
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列为空返回 `ErrorCode::EMPTY` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::EMPTY` if
   * the queue is empty
   */
  ErrorCode PopBatch(Data *data, size_t size)
  {
    size_t capacity = LENGTH + 1;

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      auto current_tail = tail_.load(std::memory_order_acquire);

      size_t available = (current_tail >= current_head)
                             ? (current_tail - current_head)
                             : (capacity - current_head + current_tail);

      if (available < size)
      {
        return ErrorCode::EMPTY;
      }

      if (data != nullptr)
      {
        size_t first_chunk = LibXR::min(size, capacity - current_head);
        LibXR::Memory::FastCopy(
            reinterpret_cast<void *>(data),
            reinterpret_cast<const void *>(queue_handle_ + current_head),
            first_chunk * sizeof(Data));

        if (size > first_chunk)
        {
          LibXR::Memory::FastCopy(reinterpret_cast<void *>(data + first_chunk),
                                  reinterpret_cast<const void *>(queue_handle_),
                                  (size - first_chunk) * sizeof(Data));
        }
      }

      size_t new_head = (current_head + size) % capacity;

      if (head_.compare_exchange_weak(current_head, new_head, std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 批量查看队列中的数据（不移除） / Peeks multiple elements from the queue
   * without removing them
   * @param data 数据存储数组指针 / Pointer to the array to store peeked data
   * @param size 要查看的元素个数 / Number of elements to peek
   * @return 操作结果，成功返回 `ErrorCode::OK`，队列中数据不足返回 `ErrorCode::EMPTY` /
   *         Operation result: returns `ErrorCode::OK` on success, `ErrorCode::EMPTY` if
   * not enough data is available
   */
  ErrorCode PeekBatch(Data *data, size_t size)
  {
    if (size == 0)
    {
      return ErrorCode::OK;
    }

    const size_t CAPACITY = LENGTH + 1;

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      auto current_tail = tail_.load(std::memory_order_acquire);

      size_t available = (current_tail >= current_head)
                             ? (current_tail - current_head)
                             : (CAPACITY - current_head + current_tail);

      if (available < size)
      {
        return ErrorCode::EMPTY;
      }

      if (data != nullptr)
      {
        size_t first_chunk = LibXR::min(size, CAPACITY - current_head);
        LibXR::Memory::FastCopy(
            reinterpret_cast<void *>(data),
            reinterpret_cast<const void *>(queue_handle_ + current_head),
            first_chunk * sizeof(Data));

        if (size > first_chunk)
        {
          LibXR::Memory::FastCopy(reinterpret_cast<void *>(data + first_chunk),
                                  reinterpret_cast<const void *>(queue_handle_),
                                  (size - first_chunk) * sizeof(Data));
        }
      }

      if (head_.load(std::memory_order_acquire) == current_head)
      {
        return ErrorCode::OK;
      }
    }
  }

  /**
   * @brief 重置队列 / Resets the queue
   *
   * 该方法清空队列并将头尾指针重置为 0。
   * This method clears the queue and resets the head and tail pointers to 0.
   */
  void Reset()
  {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  /**
   * @brief 获取当前队列中的元素数量 / Returns the number of elements currently in the
   * queue
   */
  size_t Size() const
  {
    const auto CURRENT_HEAD = head_.load(std::memory_order_acquire);
    const auto CURRENT_TAIL = tail_.load(std::memory_order_acquire);
    return (CURRENT_TAIL >= CURRENT_HEAD) ? (CURRENT_TAIL - CURRENT_HEAD)
                                          : ((LENGTH + 1) - CURRENT_HEAD + CURRENT_TAIL);
  }

  /**
   * @brief 计算队列剩余可用空间 / Calculates the remaining available space in the queue
   */
  size_t EmptySize() { return LENGTH - Size(); }

  /**
   * @brief 获取队列的最大容量 / Returns the maximum capacity of the queue
   */
  size_t MaxSize() const { return LENGTH; }

 private:
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<uint32_t> head_;
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<uint32_t> tail_;
  const size_t LENGTH;
  Data *queue_handle_;

  uint32_t Increment(uint32_t index) const { return (index + 1) % (LENGTH + 1); }
};

}  // namespace LibXR
