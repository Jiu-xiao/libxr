#pragma once

#include <cstddef>

#include "libxr_def.hpp"
#include "mutex.hpp"
#include "queue.hpp"

namespace LibXR
{
/**
 * @brief  块队列（ChunkQueue）用于存储和管理数据块
 *         ChunkQueue is used to store and manage chunks of data
 *
 * @details
 * 该类实现了一个支持多块数据存储的队列，提供线程安全的访问方式，并支持从
 * ISR（中断服务例程）中调用。 This class implements a multi-block data storage queue,
 * provides thread-safe access, and supports calls from an ISR (Interrupt Service
 * Routine).
 */
class ChunkQueue
{
 public:
  typedef uint32_t BlockInfo;  ///< 记录块信息的类型 Type for storing block information

  /**
   * @brief  构造一个块队列
   *         Constructs a chunk queue
   * @param  max_blocks 最大块数 Maximum number of blocks
   * @param  data_buffer_size 数据缓冲区大小 Size of the data buffer
   */
  ChunkQueue(size_t max_blocks, size_t data_buffer_size)
      : block_queue_(sizeof(BlockInfo), max_blocks),
        data_queue_(1, data_buffer_size),
        max_blocks_(max_blocks)
  {
    CreateNewBlock();
  }

  /**
   * @brief  创建一个新的数据块（线程安全）
   *         Creates a new data block (thread-safe)
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode CreateNewBlock()
  {
    Mutex::LockGuard lock_guard(mutex_);
    return CreateNewBlockNoLock();
  }

  /**
   * @brief  创建一个新的数据块（线程安全）
   *         Creates a new data block (thread-safe)
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode AppendToCurrentBlock(const void *data, size_t size)
  {
    Mutex::LockGuard lock_guard(mutex_);
    return AppendToCurrentBlockNoLock(data, size);
  }

  /**
   * @brief  弹出指定大小的数据（线程安全）
   *         Pops the specified size of data (thread-safe)
   * @param  size 要弹出的数据大小 Size of the data to pop
   * @param  data 存储弹出数据的缓冲区（可选） Buffer to store popped data (optional)
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode Pop(size_t size, void *data = nullptr)
  {
    Mutex::LockGuard lock_guard(mutex_);

    return PopNoLock(size, data);
  }

  /**
   * @brief  从回调函数（ISR）中弹出数据
   *         Pops data from an ISR (Interrupt Service Routine)
   * @param  size 要弹出的数据大小 Size of the data to pop
   * @param  data 存储弹出数据的缓冲区 Buffer to store popped data
   * @param  in_isr 是否在中断服务程序（ISR）中调用 Whether it is called from an ISR
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode PopFromCallback(size_t size, void *data, bool in_isr)
  {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked())
    {
      return ErrorCode::TIMEOUT;
    }

    return PopNoLock(size, data);
  }

  /**
   * @brief  弹出整个数据块（线程安全）
   *         Pops an entire data block (thread-safe)
   * @param  buffer 存储数据的缓冲区 Buffer to store popped data
   * @param  out_size 返回的块大小 Output parameter for block size
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode PopBlock(void *buffer, size_t *out_size)
  {
    Mutex::LockGuard lock_guard(mutex_);

    return PopBlockNoLock(buffer, out_size);
  }

  /**
   * @brief  从回调函数（ISR）中弹出整个数据块
   *         Pops an entire data block from an ISR
   * @param  buffer 存储数据的缓冲区 Buffer to store popped data
   * @param  out_size 返回的块大小 Output parameter for block size
   * @param  in_isr 是否在 ISR 中调用 Whether it is called from an ISR
   * @return 操作结果 ErrorCode indicating success or failure
   */
  ErrorCode PopBlockFromCallback(void *buffer, size_t *out_size, bool in_isr)
  {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked())
    {
      return ErrorCode::TIMEOUT;
    }

    return PopBlockNoLock(buffer, out_size);
  }

  /**
   * @brief  重置队列
   *         Resets the queue
   */
  void Reset()
  {
    block_queue_.Reset();
    data_queue_.Reset();
  }

  /**
   * @brief  获取当前数据大小
   *         Gets the current size of the data
   * @return 当前数据大小 Current data size
   */
  size_t Size()
  {
    Mutex::LockGuard lock_guard(mutex_);

    return data_queue_.Size();
  }

  /**
   * @brief  从 ISR 中获取当前数据大小
   *         Gets the current data size from an ISR
   * @param  in_isr 是否在 ISR 中调用 Whether it is called from an ISR
   * @return 当前数据大小 Current data size
   */
  size_t SizeFromCallback(bool in_isr)
  {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked())
    {
      return 0;
    }

    return data_queue_.Size();
  }

  /**
   * @brief  获取队列的剩余可用空间（线程安全）
   *         Gets the remaining available space in the queue (thread-safe)
   * @return 可用空间大小 The size of available space
   *
   * @details
   * 该方法通过互斥锁保证线程安全，检查 `block_queue_` 是否还有空余块，
   * 若有，则返回 `data_queue_` 的剩余空间，否则返回 0。
   *
   * This method ensures thread safety using a mutex lock.
   * It checks whether `block_queue_` has available blocks.
   * If available, it returns the remaining space in `data_queue_`, otherwise, it returns
   * 0.
   */
  size_t EmptySize()
  {
    Mutex::LockGuard lock_guard(mutex_);

    if (block_queue_.EmptySize() > 0)
    {
      return data_queue_.EmptySize();
    }
    else
    {
      return 0;
    }
  }

  /**
   * @brief  从 ISR（中断服务例程）中获取队列的剩余可用空间
   *         Gets the remaining available space in the queue from an ISR (Interrupt
   * Service Routine)
   * @param  in_isr 是否在 ISR 中调用 Whether it is called from an ISR
   * @return 可用空间大小 The size of available space
   *
   * @details
   * 该方法在中断服务例程（ISR）中安全使用，通过 `Mutex::LockGuardInCallback` 进行锁保护。
   * 若 `block_queue_` 仍有可用块，则返回 `data_queue_` 的剩余空间，否则返回 0。
   *
   * This method is safe to use in an ISR (Interrupt Service Routine),
   * protected by `Mutex::LockGuardInCallback`.
   * If `block_queue_` has available blocks, it returns the remaining space in
   * `data_queue_`, otherwise, it returns 0.
   */
  size_t EmptySizeFromCallback(bool in_isr)
  {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked())
    {
      return 0;
    }

    if (block_queue_.EmptySize() > 0)
    {
      return data_queue_.EmptySize();
    }
    else
    {
      return 0;
    }
  }

  ChunkQueue(const ChunkQueue &) = delete;
  ChunkQueue operator=(const ChunkQueue &) = delete;
  ChunkQueue operator=(ChunkQueue &) = delete;
  ChunkQueue operator=(const ChunkQueue &&) = delete;
  ChunkQueue operator=(ChunkQueue &&) = delete;

 private:
  ErrorCode CreateNewBlockNoLock()
  {
    auto index = block_queue_.GetLastElementIndex();

    if (index >= 0)
    {
      BlockInfo *last_block = reinterpret_cast<BlockInfo *>(block_queue_[index]);
      if (*last_block == 0)
      {
        return ErrorCode::OK;
      }
    }

    if (block_queue_.Size() >= max_blocks_)
    {
      return ErrorCode::FULL;
    }
    BlockInfo new_block{0};
    return block_queue_.Push(&new_block);
  }

  ErrorCode AppendToCurrentBlockNoLock(const void *data, size_t size)
  {
    if (!data)
    {
      return ErrorCode::PTR_NULL;
    }
    if (size == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    if (block_queue_.Size() == 0)
    {
      if (CreateNewBlockNoLock() != ErrorCode::OK)
      {
        return ErrorCode::FULL;
      }
    }

    auto index = block_queue_.GetLastElementIndex();

    BlockInfo *last_block = static_cast<BlockInfo *>(block_queue_[index]);

    if (size > data_queue_.EmptySize())
    {
      return ErrorCode::NO_BUFF;
    }

    if (data_queue_.PushBatch(reinterpret_cast<const uint8_t *>(data), size) !=
        ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }

    *last_block += size;
    return ErrorCode::OK;
  }

  ErrorCode PopNoLock(size_t size, void *data = nullptr)
  {
    if (data_queue_.Size() < size)
    {
      return ErrorCode::EMPTY;
    }

    size_t remaining_size = size;

    while (remaining_size > 0)
    {
      auto index = block_queue_.GetFirstElementIndex();
      if (index < 0)
      {
        return ErrorCode::CHECK_ERR;
      }

      BlockInfo *block = static_cast<BlockInfo *>(block_queue_[index]);

      if (remaining_size < *block)
      {
        if (data_queue_.PopBatch(data, remaining_size) != ErrorCode::OK)
        {
          ASSERT(false);
          return ErrorCode::CHECK_ERR;
        }

        *block -= remaining_size;
        remaining_size = 0;
      }
      else
      {
        if (data_queue_.PopBatch(data, *block) != ErrorCode::OK)
        {
          ASSERT(false);
          return ErrorCode::CHECK_ERR;
        }
        remaining_size -= *block;
        data = static_cast<uint8_t *>(data) + *block;
        block_queue_.Pop();
      }
    }

    return ErrorCode::OK;
  }

  ErrorCode PopBlockNoLock(void *buffer, size_t *out_size)
  {
    auto index = block_queue_.GetFirstElementIndex();

    if (index >= 0)
    {
      BlockInfo *last_block = static_cast<BlockInfo *>(block_queue_[index]);
      if (*last_block == 0)
      {
        return ErrorCode::EMPTY;
      }
    }
    else
    {
      return ErrorCode::EMPTY;
    }

    BlockInfo block;  // NOLINT
    if (block_queue_.Pop(&block) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    if (data_queue_.PopBatch(buffer, block) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::CHECK_ERR;
    }
    *out_size = block;
    return ErrorCode::OK;
  }

  BaseQueue block_queue_;
  BaseQueue data_queue_;
  size_t max_blocks_;
  Mutex mutex_;
};
}  // namespace LibXR
