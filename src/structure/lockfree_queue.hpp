#pragma once

#include "libxr_def.hpp"
#include <array>
#include <atomic>
#include <stdio.h>

namespace LibXR {
template <typename Data> class LockFreeQueue {
public:
  LockFreeQueue(size_t length) : head_(0), tail_(0), length_(length) {
    queue_handle_ = new Data[length + 1];
  }

  ~LockFreeQueue() { delete queue_handle_; }

  ErrorCode Push(const Data &item) {
    const auto current_tail = tail_.load(std::memory_order_relaxed);
    const auto next_tail = increment(current_tail);
    if (next_tail == head_.load(std::memory_order_acquire)) {
      return ERR_FULL;
    }
    queue_handle_[current_tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return NO_ERR;
  }

  ErrorCode Pop(Data &item) {
    const auto current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire)) {
      return ERR_EMPTY;
    }
    item = queue_handle_[current_head];
    head_.store(increment(current_head), std::memory_order_release);
    return NO_ERR;
  }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  // Get the current size of the queue
  size_t Size() const {
    const auto current_head = head_.load(std::memory_order_relaxed);
    const auto current_tail = tail_.load(std::memory_order_relaxed);
    return (current_tail >= current_head)
               ? (current_tail - current_head)
               : (length_ - current_head + current_tail);
  }

  size_t EmptySize() { return (length_ + 1) - Size(); }

private:
  Data *queue_handle_;
  std::atomic<unsigned int> head_;
  std::atomic<unsigned int> tail_;
  size_t length_;

  unsigned int increment(unsigned int index) const {
    return (index + 1) % (length_ + 1);
  }
};
} // namespace LibXR