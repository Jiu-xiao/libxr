#pragma once

#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include <cstdio>

namespace LibXR {
class BaseQueue {
public:
  BaseQueue(uint16_t element_size, size_t length)
      : element_size_(element_size), length_(length) {
    queue_array_ = new uint8_t[length * element_size];
  }

  ~BaseQueue() { delete[] queue_array_; }

  void *operator[](int32_t index) {
    if (index > 0) {
      index = (head_ + index) % length_;
    } else {
      index = (head_ + length_ + index) % length_;
    }
    return &queue_array_[index * element_size_];
  }

  ErrorCode Push(const void *data) {
    if (is_full_) {
      return ErrorCode::FULL;
    }

    memcpy(&queue_array_[tail_ * element_size_], data, element_size_);

    tail_ = (tail_ + 1) % length_;
    if (head_ == tail_) {
      is_full_ = true;
    }

    return ErrorCode::OK;
  }

  ErrorCode Peek(void *data) {
    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * element_size_], element_size_);
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Pop(void *data) {
    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * element_size_], element_size_);
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

  ErrorCode PushBatch(const void *data, size_t size) {
    auto avail = EmptySize();
    if (avail < size) {
      return ErrorCode::FULL;
    }

    auto tmp = reinterpret_cast<const uint8_t *>(data);

    for (size_t i = 0; i < size; i++) {
      memcpy(&queue_array_[tail_ * element_size_], &tmp[i * element_size_],
             element_size_);
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
      memcpy(&tmp[i * element_size_], &queue_array_[head_ * element_size_],
             element_size_);
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
      memcpy(&tmp[index * element_size_], &queue_array_[index * element_size_],
             element_size_);
      index = (index + 1) % length_;
    }

    return ErrorCode::OK;
  }

  ErrorCode Overwrite(const void *data) {
    head_ = tail_ = 0;
    is_full_ = false;

    memcpy(queue_array_, data, element_size_ * length_);

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

  uint8_t *queue_array_;
  const uint16_t element_size_;
  size_t head_ = 0;
  size_t tail_ = 0;
  bool is_full_ = false;
  size_t length_;
};

class ChunkedQueue {
public:
  ChunkedQueue(uint16_t element_size, size_t length)
      : element_size_(element_size), length_(length) {
    queue_array_ = new uint8_t[length * element_size];
    offset_ = new uint16_t[length];
    memset(offset_, 0, sizeof(uint16_t) * length);
  }

  ~ChunkedQueue() {
    delete[] queue_array_;
    delete[] offset_;
  }

  void *operator[](int32_t index) {
    if (index >= 0) {
      index = (head_ + index) % length_;
    } else {
      index = (head_ + length_ + index) % length_;
    }
    return &queue_array_[index * element_size_];
  }

  uint16_t operator()(int32_t index) { return offset_[index]; }

  ErrorCode Peek(uint16_t *offset, void *data) {
    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * element_size_], offset_[head_]);
      *offset = offset_[head_];
      return ErrorCode::OK;
    }
    return ErrorCode::EMPTY;
  }

  ErrorCode Pop(uint16_t *offset, void *data) {
    if (Size() > 0) {
      *offset = offset_[head_];
      memcpy(data, &queue_array_[head_ * element_size_], *offset);
      offset_[head_] = 0;
      head_ = (head_ + 1) % length_;
      is_full_ = false;
      return ErrorCode::OK;
    } else if (offset_[head_] > 0) {
      *offset = offset_[head_];
      memcpy(data, &queue_array_[head_ * element_size_], *offset);
      is_full_ = false;
      offset_[head_] = 0;
      return ErrorCode::OK;
    }

    return ErrorCode::EMPTY;
  }

  ErrorCode Pop() {
    if (Size() > 0) {
      head_ = (head_ + 1) % length_;
      is_full_ = false;
      return ErrorCode::OK;
    }
    return ErrorCode::EMPTY;
  }

  ErrorCode PushPartial(const void *data, size_t size) {
    if (size > element_size_) {
      return ErrorCode::SIZE_ERR;
    }

    // 当前尾部元素剩余空间不足，切换到下一个元素
    if (element_size_ - offset_[tail_] < size) {
      if (is_full_) {
        return ErrorCode::FULL;
      }
      tail_ = (tail_ + 1) % length_;
      if (head_ == tail_) {
        is_full_ = true;
        return ErrorCode::FULL;
      }
      offset_[tail_] = 0;
    }

    memcpy(&queue_array_[tail_ * element_size_ + offset_[tail_]], data, size);
    offset_[tail_] += size;

    return ErrorCode::OK;
  }

  ErrorCode FillCurrentElement() {
    if (offset_[tail_] == 0) {
      return ErrorCode::OK;
    }

    if (is_full_) {
      return ErrorCode::FULL;
    }

    tail_ = (tail_ + 1) % length_;
    if (head_ == tail_) {
      is_full_ = true;
    }

    offset_[tail_] = 0;
    return ErrorCode::OK;
  }

  void Reset() {
    head_ = 0;
    tail_ = 0;
    is_full_ = false;
    memset(offset_, 0, sizeof(uint16_t) * length_);
  }

  size_t Size() const {
    if (is_full_) {
      return length_;
    } else if (tail_ >= head_) {
      return tail_ - head_;
    } else {
      return length_ + tail_ - head_;
    }
  }

  size_t EmptySize() const { return length_ - Size(); }

  // private:
  uint8_t *queue_array_;
  uint16_t *offset_;
  const uint16_t element_size_;
  size_t head_ = 0;
  size_t tail_ = 0;
  bool is_full_ = false;
  size_t length_;

  // Uncomment if thread safety is required
  // std::mutex mutex_;
};

template <typename Data> class Queue : public BaseQueue {
public:
  Queue(size_t length) : BaseQueue(sizeof(Data), length) {}

  Data &operator[](int32_t index) {
    if (index >= 0) {
      index = (head_ + index) % length_;
    } else {
      index = (tail_ + index + length_) % length_;
    }

    return *reinterpret_cast<Data *>(&queue_array_[index * element_size_]);
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
} // namespace LibXR