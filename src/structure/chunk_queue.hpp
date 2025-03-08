#pragma once

#include <cstddef>

#include "libxr_def.hpp"
#include "mutex.hpp"
#include "queue.hpp"

namespace LibXR {
class ChunkQueue {
 public:
  typedef uint32_t BlockInfo;

  ChunkQueue(size_t max_blocks, size_t data_buffer_size)
      : block_queue_(sizeof(BlockInfo), max_blocks),
        data_queue_(1, data_buffer_size),
        max_blocks_(max_blocks) {
    CreateNewBlock();
  }

  ErrorCode CreateNewBlock() {
    Mutex::LockGuard lock_guard(mutex_);
    return CreateNewBlockNoLock();
  }

  ErrorCode AppendToCurrentBlock(const void *data, size_t size) {
    Mutex::LockGuard lock_guard(mutex_);
    return AppendToCurrentBlockNoLock(data, size);
  }

  ErrorCode Pop(size_t size, void *data = nullptr) {
    Mutex::LockGuard lock_guard(mutex_);

    return PopNoLock(size, data);
  }

  ErrorCode PopFromCallback(size_t size, void *data, bool in_isr) {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked()) {
      return ErrorCode::TIMEOUT;
    }

    return PopNoLock(size, data);
  }

  ErrorCode PopBlock(void *buffer, size_t *out_size) {
    Mutex::LockGuard lock_guard(mutex_);

    return PopBlockNoLock(buffer, out_size);
  }

  ErrorCode PopBlockFromCallback(void *buffer, size_t *out_size, bool in_isr) {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked()) {
      return ErrorCode::TIMEOUT;
    }

    return PopBlockNoLock(buffer, out_size);
  }

  void Reset() {
    block_queue_.Reset();
    data_queue_.Reset();
  }

  size_t Size() {
    Mutex::LockGuard lock_guard(mutex_);

    return data_queue_.Size();
  }

  size_t SizeFromCallback(bool in_isr) {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked()) {
      return 0;
    }

    return data_queue_.Size();
  }

  size_t EmptySize() {
    Mutex::LockGuard lock_guard(mutex_);

    if (block_queue_.EmptySize() > 0) {
      return data_queue_.EmptySize();
    } else {
      return 0;
    }
  }

  size_t EmptySizeFromCallback(bool in_isr) {
    Mutex::LockGuardInCallback lock_guard(mutex_, in_isr);

    if (!lock_guard.Locked()) {
      return 0;
    }

    if (block_queue_.EmptySize() > 0) {
      return data_queue_.EmptySize();
    } else {
      return 0;
    }
  }

  ChunkQueue(const ChunkQueue &) = delete;
  ChunkQueue operator=(const ChunkQueue &) = delete;
  ChunkQueue operator=(ChunkQueue &) = delete;
  ChunkQueue operator=(const ChunkQueue &&) = delete;
  ChunkQueue operator=(ChunkQueue &&) = delete;

 private:
  ErrorCode CreateNewBlockNoLock() {
    auto index = block_queue_.GetLastElementIndex();

    if (index >= 0) {
      BlockInfo *last_block =
          reinterpret_cast<BlockInfo *>(block_queue_[index]);
      if (*last_block == 0) {
        return ErrorCode::OK;
      }
    }

    if (block_queue_.Size() >= max_blocks_) {
      return ErrorCode::FULL;
    }
    BlockInfo new_block{0};
    return block_queue_.Push(&new_block);
  }

  ErrorCode AppendToCurrentBlockNoLock(const void *data, size_t size) {
    if (!data) {
      return ErrorCode::PTR_NULL;
    }
    if (size == 0) {
      return ErrorCode::ARG_ERR;
    }

    if (block_queue_.Size() == 0) {
      if (CreateNewBlockNoLock() != ErrorCode::OK) {
        return ErrorCode::FULL;
      }
    }

    auto index = block_queue_.GetLastElementIndex();

    BlockInfo *last_block = static_cast<BlockInfo *>(block_queue_[index]);

    if (size > data_queue_.EmptySize()) {
      return ErrorCode::NO_BUFF;
    }

    if (data_queue_.PushBatch(reinterpret_cast<const uint8_t *>(data), size) !=
        ErrorCode::OK) {
      return ErrorCode::FULL;
    }

    *last_block += size;
    return ErrorCode::OK;
  }

  ErrorCode PopNoLock(size_t size, void *data = nullptr) {
    if (data_queue_.Size() < size) {
      return ErrorCode::EMPTY;
    }

    size_t remaining_size = size;

    while (remaining_size > 0) {
      auto index = block_queue_.GetFirstElementIndex();
      if (index < 0) {
        return ErrorCode::CHECK_ERR;
      }

      BlockInfo *block = static_cast<BlockInfo *>(block_queue_[index]);

      if (remaining_size < *block) {
        if (data_queue_.PopBatch(data, remaining_size) != ErrorCode::OK) {
          ASSERT(false);
          return ErrorCode::CHECK_ERR;
        }

        *block -= remaining_size;
        remaining_size = 0;
      } else {
        if (data_queue_.PopBatch(data, *block) != ErrorCode::OK) {
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

  ErrorCode PopBlockNoLock(void *buffer, size_t *out_size) {
    auto index = block_queue_.GetFirstElementIndex();

    if (index >= 0) {
      BlockInfo *last_block = static_cast<BlockInfo *>(block_queue_[index]);
      if (*last_block == 0) {
        return ErrorCode::EMPTY;
      }
    } else {
      return ErrorCode::EMPTY;
    }

    BlockInfo block;  // NOLINT
    if (block_queue_.Pop(&block) != ErrorCode::OK) {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    if (data_queue_.PopBatch(buffer, block) != ErrorCode::OK) {
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
