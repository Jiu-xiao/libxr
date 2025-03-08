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
    return &queue_array_[static_cast<size_t>(index * ELEMENT_SIZE)];
  }

  ErrorCode Push(const void *data) {
    ASSERT(data != nullptr);

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
    ASSERT(data != nullptr);

    if (Size() > 0) {
      memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Pop(void *data = nullptr) {
    if (Size() > 0) {
      if (data != nullptr) {
        memcpy(data, &queue_array_[head_ * ELEMENT_SIZE], ELEMENT_SIZE);
      }
      head_ = (head_ + 1) % length_;
      is_full_ = false;
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  int GetLastElementIndex() {
    if (Size() > 0) {
      return static_cast<int>((tail_ + length_ - 1) % length_);
    } else {
      return -1;
    }
  }

  int GetFirstElementIndex() {
    if (Size() > 0) {
      return static_cast<int>(head_);
    } else {
      return -1;
    }
  }

  ErrorCode PushBatch(const void *data, size_t size) {
    ASSERT(data != nullptr);

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
      if (data != nullptr) {
        memcpy(&tmp[i * ELEMENT_SIZE], &queue_array_[head_ * ELEMENT_SIZE],
               ELEMENT_SIZE);
      }
      head_ = (head_ + 1) % length_;
    }

    return ErrorCode::OK;
  }

  ErrorCode PeekBatch(void *data, size_t size) {
    ASSERT(data != nullptr);

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
    ASSERT(data != nullptr);

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
