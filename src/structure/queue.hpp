#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "libxr_def.hpp"

namespace LibXR {
class BaseQueue {
 public:
  BaseQueue(uint16_t element_size, size_t length)
      : queue_array_(new uint8_t[length * element_size]),
        ELEMENT_SIZE(element_size),
        length_(length) {}

  ~BaseQueue() { delete[] queue_array_; }

  void *operator[](uint32_t index) {
    if (index > 0) {
      index = (head_ + index) % length_;
    } else {
      index = (head_ + length_ + index) % length_;
    }
    return &queue_array_[static_cast<ptrdiff_t>(index * ELEMENT_SIZE)];
  }

  ErrorCode Push(const void *data) {
    if (is_full_) {
      return ErrorCode::FULL;
    }

    memcpy(&queue_array_[tail_ * ELEMENT_SIZE], data, ELEMENT_SIZE);

    tail_ = (tail_ + 1) % length_;
    if (head_ == tail_) {
      is_full_ = true;
    }

    return ErrorCode::OK;
  }

  ErrorCode Peek(void *data) {
    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Pop(void *data) {
    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
      head_ = (head_ + 1) % length_;
      is_full_ = false;
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Pop() {
    if (Size() > 0) {
      head_ = (head_ + 1) % length_;
      is_full_ = false;
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  void *GetLastElement() {
    if (Size() > 0) {
      return &queue_array_[(tail_ - 1) * ELEMENT_SIZE];
    } else {
      return nullptr;
    }
  }

  ErrorCode PushBatch(const void *data, size_t size) {
    auto avail = EmptySize();
    if (avail < size) {
      return ErrorCode::FULL;
    }

    auto tmp = reinterpret_cast<const uint8_t *>(data);

    for (size_t i = 0; i < size; i++) {
      memcpy(&queue_array_[tail_ * ELEMENT_SIZE], &tmp[i * ELEMENT_SIZE],
             ELEMENT_SIZE);
      tail_ = (tail_ + 1) % length_;
    }

    if (head_ == tail_) {
      is_full_ = true;
    }
    return ErrorCode::OK;
  }

  ErrorCode PopBatch(void *data, size_t size) {
    if (Size() < size) {
      return ErrorCode::EMPTY;
    }

    if (size > 0) {
      is_full_ = false;
    } else {
      return ErrorCode::OK;
    }

    auto tmp = reinterpret_cast<uint8_t *>(data);

    for (size_t i = 0; i < size; i++) {
      memcpy(&tmp[i * ELEMENT_SIZE], &queue_array_[head_ * ELEMENT_SIZE],
             ELEMENT_SIZE);
      head_ = (head_ + 1) % length_;
    }

    return ErrorCode::OK;
  }

  ErrorCode PopBatch(size_t size) {
    if (Size() < size) {
      return ErrorCode::EMPTY;
    }

    if (size > 0) {
      is_full_ = false;
    } else {
      return ErrorCode::OK;
    }

    for (size_t i = 0; i < size; i++) {
      head_ = (head_ + 1) % length_;
    }

    return ErrorCode::OK;
  }

  ErrorCode PeekBatch(void *data, size_t size) {
    if (Size() < size) {
      return ErrorCode::EMPTY;
    }

    auto index = head_;

    auto tmp = reinterpret_cast<uint8_t *>(data);

    for (size_t i = 0; i < size; i++) {
      memcpy(&tmp[index * ELEMENT_SIZE], &queue_array_[index * ELEMENT_SIZE],
             ELEMENT_SIZE);
      index = (index + 1) % length_;
    }

    return ErrorCode::OK;
  }

  ErrorCode Overwrite(const void *data) {
    head_ = tail_ = 0;
    is_full_ = false;

    memcpy(queue_array_, data, ELEMENT_SIZE * length_);

    tail_ = (tail_ + 1) % length_;
    if (head_ == tail_) {
      is_full_ = true;
    }

    return ErrorCode::OK;
  }

  void Reset() {
    head_ = tail_ = 0;
    is_full_ = false;
  }

  size_t Size() {
    if (is_full_) {
      return length_;
    } else if (tail_ >= head_) {
      return tail_ - head_;
    } else {
      return length_ + tail_ - head_;
    }
  }

  size_t EmptySize() { return length_ - Size(); }

  BaseQueue(const BaseQueue &) = delete;
  BaseQueue operator=(const BaseQueue &) = delete;
  BaseQueue operator=(BaseQueue &) = delete;
  BaseQueue operator=(const BaseQueue &&) = delete;
  BaseQueue operator=(BaseQueue &&) = delete;

  uint8_t *queue_array_;
  const uint16_t ELEMENT_SIZE;
  size_t head_ = 0;
  size_t tail_ = 0;
  bool is_full_ = false;
  size_t length_;
};

class ChunkManager {
 public:
  struct BlockInfo {
    uint16_t start_offset;
    uint16_t size;
  };

  ChunkManager(size_t max_blocks, size_t data_buffer_size)
      : block_queue_(sizeof(BlockInfo), max_blocks),
        data_queue_(sizeof(uint8_t), data_buffer_size) {}

  ErrorCode CreateNewBlock() {
    if (block_queue_.Size() >= max_blocks_) {
      return ErrorCode::FULL;
    }
    BlockInfo new_block{static_cast<uint16_t>(data_queue_.tail_), 0};
    return block_queue_.Push(&new_block);
  }

  ErrorCode AppendToCurrentBlock(const void *data, size_t size) {
    if (!data) {
      return ErrorCode::PTR_NULL;
    }
    if (size == 0) {
      return ErrorCode::ARG_ERR;
    }
    if (block_queue_.Size() == 0) {
      if (CreateNewBlock() != ErrorCode::OK) {
        return ErrorCode::FULL;
      }
    }
    BlockInfo *last_block =
        static_cast<BlockInfo *>(block_queue_.GetLastElement());
    if (!last_block) {
      return ErrorCode::EMPTY;
    }
    if (size > data_queue_.EmptySize()) {
      return ErrorCode::NO_BUFF;
    }

    if (data_queue_.PushBatch(reinterpret_cast<const uint8_t *>(data), size) !=
        ErrorCode::OK) {
      return ErrorCode::FULL;
    }

    last_block->size += size;
    return ErrorCode::OK;
  }

  ErrorCode ReadBlock(size_t block_index, void *buffer, size_t *out_size) {
    if (!buffer || !out_size) {
      return ErrorCode::PTR_NULL;
    }
    if (block_index >= block_queue_.Size()) {
      return ErrorCode::OUT_OF_RANGE;
    }
    BlockInfo block;
    if (block_queue_.Pop(&block) != ErrorCode::OK) {
      return ErrorCode::NOT_FOUND;
    }

    if (data_queue_.PopBatch(buffer, block.size) != ErrorCode::OK) {
      return ErrorCode::CHECK_ERR;
    }
    *out_size = block.size;
    return ErrorCode::OK;
  }

  ErrorCode PopBlock() { return block_queue_.Pop(); }

  void Reset() {
    block_queue_.Reset();
    data_queue_.Reset();
  }

  size_t Size() { return data_queue_.Size(); }

  size_t EmptySize() {
    if (block_queue_.Size() > 0) {
      return data_queue_.EmptySize();
    } else {
      return 0;
    }
  }

  ChunkManager(const ChunkManager &) = delete;
  ChunkManager operator=(const ChunkManager &) = delete;
  ChunkManager operator=(ChunkManager &) = delete;
  ChunkManager operator=(const ChunkManager &&) = delete;
  ChunkManager operator=(ChunkManager &&) = delete;

 private:
  BaseQueue block_queue_;
  BaseQueue data_queue_;
  size_t &max_blocks_ = block_queue_.length_;
  size_t &data_buffer_size_ = data_queue_.length_;
};

template <typename Data>
class Queue : public BaseQueue {
 public:
  Queue(size_t length) : BaseQueue(sizeof(Data), length) {}

  Data &operator[](int32_t index) {
    if (index >= 0) {
      index = (head_ + index) % length_;
    } else {
      index = (tail_ + index + length_) % length_;
    }

    return *reinterpret_cast<Data *>(&queue_array_[index * ELEMENT_SIZE]);
  }

  ErrorCode Push(const Data &data) { return BaseQueue::Push(&data); }

  ErrorCode Pop(Data &data) { return BaseQueue::Pop(&data); }

  ErrorCode Pop() { return BaseQueue::Pop(); }

  ErrorCode Peek(Data &data) { return BaseQueue::Peek(&data); }

  ErrorCode PushBatch(const Data *data, size_t size) {
    return BaseQueue::PushBatch(data, size);
  }

  ErrorCode PopBatch(Data *data, size_t size) {
    return BaseQueue::PopBatch(data, size);
  }

  ErrorCode PeekBatch(Data *data, size_t size) {
    return BaseQueue::PeekBatch(data, size);
  }

  ErrorCode Overwrite(const Data &data) { return BaseQueue::Overwrite(&data); }
};
}  // namespace LibXR