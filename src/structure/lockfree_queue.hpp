#pragma once

#include <atomic>

#include "libxr_def.hpp"

static constexpr size_t LIBXR_CACHE_LINE_SIZE =
    (sizeof(std::atomic<size_t>) * 8 > 32) ? 64 : 32;

namespace LibXR
{

/**
 * @class LockFreeQueue
 * @brief 无锁队列实现 / Lock-free queue implementation
 *
 * 该类实现了无锁队列（Lock-Free Queue），支持多线程环境下的高效入队和出队操作，
 * 适用于需要高并发性能的场景，如实时系统和多线程数据处理。
 * This class implements a lock-free queue that supports efficient enqueue and dequeue
 * operations in a multithreaded environment, suitable for high-concurrency applications
 * such as real-time systems and multithreaded data processing.
 *
 * @tparam Data 队列存储的数据类型 / The type of data stored in the queue.
 */
template <typename Data>
class LockFreeQueue
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param length 队列的最大容量 / Maximum capacity of the queue
   *
   * 创建一个指定大小的无锁队列，并初始化相关变量。
   * Creates a lock-free queue with the specified size and initializes relevant variables.
   */
  LockFreeQueue(size_t length)
      : head_(0), tail_(0), queue_handle_(new Data[length + 1]), length_(length)
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

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed))
      {
        item = queue_handle_[current_head];
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
   * @note 使用 `std::atomic_thread_fence(std::memory_order_acquire)` 确保正确读取数据，
   *       防止 CPU 乱序执行带来的数据读取问题。
   *       (Uses `std::atomic_thread_fence(std::memory_order_acquire)` to ensure proper
   * data reading and prevent CPU reordering issues).
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

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed))
      {
        std::atomic_thread_fence(std::memory_order_acquire);
        item = queue_handle_[current_head];
        return ErrorCode::OK;
      }
      current_head = head_.load(std::memory_order_relaxed);
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
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
      current_head = head_.load(std::memory_order_relaxed);
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
    const auto CURRENT_HEAD = head_.load(std::memory_order_acquire);
    if (CURRENT_HEAD == tail_.load(std::memory_order_acquire))
    {
      return ErrorCode::EMPTY;
    }

    item = queue_handle_[CURRENT_HEAD];
    return ErrorCode::OK;
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

    size_t capacity = length_ + 1;
    size_t free_space = (current_tail >= current_head)
                            ? (capacity - (current_tail - current_head) - 1)
                            : (current_head - current_tail - 1);

    if (free_space < size)
    {
      return ErrorCode::FULL;
    }

    for (size_t i = 0; i < size; ++i)
    {
      queue_handle_[(current_tail + i) % capacity] = data[i];
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
  ErrorCode PopBatch(Data *data, size_t batch_size)
  {
    size_t capacity = length_ + 1;

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      auto current_tail = tail_.load(std::memory_order_acquire);

      size_t available = (current_tail >= current_head)
                             ? (current_tail - current_head)
                             : (capacity - current_head + current_tail);

      if (available < batch_size)
      {
        return ErrorCode::EMPTY;
      }

      for (size_t i = 0; i < batch_size; ++i)
      {
        data[i] = queue_handle_[(current_head + i) % capacity];
      }

      auto next_head = (current_head + batch_size) % capacity;

      if (head_.compare_exchange_weak(current_head, next_head, std::memory_order_acquire,
                                      std::memory_order_relaxed))
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
                                          : ((length_ + 1) - CURRENT_HEAD + CURRENT_TAIL);
  }

  /**
   * @brief 计算队列剩余可用空间 / Calculates the remaining available space in the queue
   */
  size_t EmptySize() { return length_ - Size(); }

 private:
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<unsigned int> head_;
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<unsigned int> tail_;
  Data *queue_handle_;
  size_t length_;

  unsigned int Increment(unsigned int index) const { return (index + 1) % (length_ + 1); }
};

}  // namespace LibXR
