#pragma once

#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include <array>

namespace LibXR {
class BaseQueue {
public:
  BaseQueue(size_t element_size, size_t length)
      : length_(length), element_size_(element_size) {
    queue_array_ = new uint8_t[length * element_size];
  }

  ~BaseQueue() { delete queue_array_; }

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

private:
  uint8_t *queue_array_;
  const size_t element_size_;
  size_t head_ = 0;
  size_t tail_ = 0;
  bool is_full_ = false;
  size_t length_;
};

template <typename Data> class Queue : public BaseQueue {
public:
  Queue(size_t length) : BaseQueue(sizeof(Data), length) {}

  ErrorCode Push(const Data &data) { return BaseQueue::Push(&data); }

  ErrorCode Pop(Data &data) { return BaseQueue::Pop(&data); }

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