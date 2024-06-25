#pragma once

#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include <array>

namespace LibXR {
template <typename Data> class Queue {
public:
  Queue(size_t length) : length_(length) {
    queue_handle_.data = new Data[length];
  }

  ~Queue() { delete queue_handle_.data; }

  ErrorCode Push(const Data &data) {
    queue_handle_.mutex.Lock();
    if (queue_handle_.is_full) {
      queue_handle_.mutex.Unlock();
      return ErrorCode::FULL;
    }
    queue_handle_.data[queue_handle_.tail] = data;
    queue_handle_.tail = (queue_handle_.tail + 1) % length_;
    if (queue_handle_.head == queue_handle_.tail) {
      queue_handle_.is_full = true;
    }
    queue_handle_.mutex.Unlock();
    queue_handle_.sem.Post();

    return ErrorCode::OK;
  }

  ErrorCode Pop(Data &data, uint32_t timeout) {
    if (queue_handle_.sem.Wait(timeout) == ErrorCode::OK) {
      queue_handle_.mutex.Lock();
      data = queue_handle_.data[queue_handle_.head];
      queue_handle_.head = (queue_handle_.head + 1) % length_;
      queue_handle_.is_full = false;
      queue_handle_.mutex.Unlock();
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Overwrite(const Data &data) {
    queue_handle_.mutex.Lock();
    if (Size() > 0) {
      while (queue_handle_.sem.Wait(0) == ErrorCode::OK) {
      }
    }
    queue_handle_.head = queue_handle_.tail = 0;
    queue_handle_.is_full = false;

    queue_handle_.data[queue_handle_.tail] = data;
    queue_handle_.tail = (queue_handle_.tail + 1) % length_;
    if (queue_handle_.head == queue_handle_.tail) {
      queue_handle_.is_full = true;
    }
    queue_handle_.mutex.Unlock();

    return ErrorCode::OK;
  }

  ErrorCode PushFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Push(data);
  }

  ErrorCode OverwriteFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Overwrite(data);
  }

  void Reset() {
    queue_handle_.mutex.Lock();
    if (Size() > 0) {
      while (queue_handle_.sem.Wait(0) == ErrorCode::OK) {
      }
    }
    queue_handle_.head = queue_handle_.tail = 0;
    queue_handle_.is_full = false;
    queue_handle_.mutex.Unlock();
  }

  size_t Size() {
    queue_handle_.mutex.Lock();

    if (queue_handle_.is_full) {
      queue_handle_.mutex.Unlock();
      return length_;
    } else if (queue_handle_.tail >= queue_handle_.head) {
      queue_handle_.mutex.Unlock();
      return queue_handle_.tail - queue_handle_.head;
    } else {
      queue_handle_.mutex.Unlock();
      return length_ + queue_handle_.tail - queue_handle_.head;
    }
  }

  size_t EmptySize() { return length_ - Size(); }

private:
  libxr_queue_handle queue_handle_;
  size_t length_;
};
} // namespace LibXR