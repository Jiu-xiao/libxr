#pragma once

#include <atomic>

#include "libxr_def.hpp"

static constexpr size_t LIBXR_CACHE_LINE_SIZE =
    (sizeof(std::atomic<size_t>) * 8 > 32) ? 64 : 32;

namespace LibXR {
template <typename Data>
class LockFreeQueue {
 public:
  LockFreeQueue(size_t length)
      : head_(0),
        tail_(0),
        queue_handle_(new Data[length + 1]),
        length_(length) {}

  ~LockFreeQueue() { delete[] queue_handle_; }

  Data *operator[](uint32_t index) {
    return &queue_handle_[static_cast<size_t>(index)];
  }

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
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true) {
      if (current_head == tail_.load(std::memory_order_acquire)) {
        return ErrorCode::EMPTY;
      }

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        item = queue_handle_[current_head];
        return ErrorCode::OK;
      }
    }
  }

  ErrorCode Pop(Data &item) {
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true) {
      if (current_head == tail_.load(std::memory_order_acquire)) {
        return ErrorCode::EMPTY;
      }

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        std::atomic_thread_fence(std::memory_order_acquire);
        item = queue_handle_[current_head];
        return ErrorCode::OK;
      }
      current_head = head_.load(std::memory_order_relaxed);
    }
  }

  ErrorCode Pop() {
    auto current_head = head_.load(std::memory_order_relaxed);

    while (true) {
      if (current_head == tail_.load(std::memory_order_acquire)) {
        return ErrorCode::EMPTY;
      }

      if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        return ErrorCode::OK;
      }
      current_head = head_.load(std::memory_order_relaxed);
    }
  }

  ErrorCode Peek(Data &item) {
    const auto CURRENT_HEAD = head_.load(std::memory_order_acquire);
    if (CURRENT_HEAD == tail_.load(std::memory_order_acquire)) {
      return ErrorCode::EMPTY;
    }

    item = queue_handle_[CURRENT_HEAD];
    return ErrorCode::OK;
  }

  ErrorCode PushBatch(Data *data, size_t size) {
    if (EmptySize() < size) {
      return ErrorCode::FULL;
    }

    for (size_t i = 0; i < size; ++i) {
      if (Push(data[i]) != ErrorCode::OK) {
        return ErrorCode::FULL;
      }
    }
    return ErrorCode::OK;
  }

  ErrorCode PopBatch(Data *data, size_t size) {
    if (Size() < size) {
      return ErrorCode::EMPTY;
    }

    for (size_t i = 0; i < size; ++i) {
      if (Pop(data[i]) != ErrorCode::OK) {
        return ErrorCode::EMPTY;
      }
    }
    return ErrorCode::OK;
  }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  size_t Size() const {
    const auto CURRENT_HEAD = head_.load(std::memory_order_acquire);
    const auto CURRENT_TAIL = tail_.load(std::memory_order_acquire);
    return (CURRENT_TAIL >= CURRENT_HEAD)
               ? (CURRENT_TAIL - CURRENT_HEAD)
               : (length_ - CURRENT_HEAD + CURRENT_TAIL);
  }

  size_t EmptySize() { return (length_ + 1) - Size(); }

 private:
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<unsigned int> head_;
  alignas(LIBXR_CACHE_LINE_SIZE) std::atomic<unsigned int> tail_;
  Data *queue_handle_;
  size_t length_;

  unsigned int Increment(unsigned int index) const {
    return (index + 1) % (length_ + 1);
  }
};
}  // namespace LibXR
