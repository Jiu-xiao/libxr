#pragma once

#include <atomic>

#include "libxr_def.hpp"

namespace LibXR {
template <typename Data>
class LockFreeQueue {
 public:
  LockFreeQueue(size_t length)
      : queue_handle_(new Data[length + 1]),
        head_(0),
        tail_(0),
        length_(length) {}

  ~LockFreeQueue() { delete[] queue_handle_; }

  template <typename ElementData = Data>
  ErrorCode Push(ElementData &&item) {
    const auto CURRENT_TAIL = tail_.load(std::memory_order_relaxed);
    const auto NEXT_TAIL = Increment(CURRENT_TAIL);
    if (NEXT_TAIL == head_.load(std::memory_order_acquire)) {
      return ErrorCode::FULL;
    }
    queue_handle_[CURRENT_TAIL] = std::forward<ElementData>(item);
    tail_.store(NEXT_TAIL, std::memory_order_release);
    return ErrorCode::OK;
  }

  template <typename ElementData = Data>
  ErrorCode Pop(ElementData &item) {
    const auto CURRENT_HEAD = head_.load(std::memory_order_relaxed);
    if (CURRENT_HEAD == tail_.load(std::memory_order_acquire)) {
      return ErrorCode::EMPTY;
    }
    item = queue_handle_[CURRENT_HEAD];
    head_.store(Increment(CURRENT_HEAD), std::memory_order_release);
    return ErrorCode::OK;
  }

  ErrorCode Pop() {
    const auto CURRENT_HEAD = head_.load(std::memory_order_relaxed);
    if (CURRENT_HEAD == tail_.load(std::memory_order_acquire)) {
      return ErrorCode::EMPTY;
    }
    head_.store(Increment(CURRENT_HEAD), std::memory_order_release);
    return ErrorCode::OK;
  }

  ErrorCode Peek(Data &item) {
    const auto CURRENT_HEAD = head_.load(std::memory_order_relaxed);
    if (CURRENT_HEAD == tail_.load(std::memory_order_acquire)) {
      return ErrorCode::EMPTY;
    }

    item = queue_handle_[CURRENT_HEAD];
    return ErrorCode::OK;  // 成功
  }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  // Get the current size of the queue
  size_t Size() const {
    const auto CURRENT_HEAD = head_.load(std::memory_order_relaxed);
    const auto CURRENT_TAIL = tail_.load(std::memory_order_relaxed);
    return (CURRENT_TAIL >= CURRENT_HEAD)
               ? (CURRENT_TAIL - CURRENT_HEAD)
               : (length_ - CURRENT_HEAD + CURRENT_TAIL);
  }

  size_t EmptySize() { return (length_ + 1) - Size(); }

 private:
  Data *queue_handle_;
  std::atomic<unsigned int> head_;
  std::atomic<unsigned int> tail_;
  size_t length_;

  unsigned int Increment(unsigned int index) const {
    return (index + 1) % (length_ + 1);
  }
};
}  // namespace LibXR