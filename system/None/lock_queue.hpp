#pragma once

#include <array>
#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_system.hpp"
#include "mutex.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "timebase.hpp"

namespace LibXR {
template <typename Data>
class LockQueue {
 public:
  LockQueue(size_t length) : queue_handle_(length) {}

  ~LockQueue() {}

  ErrorCode Push(const Data &data) {
    mutex_.Lock();
    auto ans = queue_handle_.Push(data);
    if (ans == ErrorCode::OK) {
      semaphore_handle_.Post();
    }
    mutex_.Unlock();
    return ans;
  }

  ErrorCode Pop(Data &data, uint32_t timeout) {
    if (semaphore_handle_.Wait(timeout) == ErrorCode::OK) {
      mutex_.Lock();
      auto ans = queue_handle_.Pop(data);
      mutex_.Unlock();
      return ans;
    } else {
      return ErrorCode::TIMEOUT;
    }
  }

  ErrorCode Pop() {
    mutex_.Lock();
    auto ans = queue_handle_.Pop();
    mutex_.Unlock();
    return ans;
  }

  ErrorCode PopFromCallback(bool in_isr) {
    UNUSED(in_isr);
    return Pop();
  }

  ErrorCode Pop(uint32_t timeout) {
    if (semaphore_handle_.Wait(timeout) == ErrorCode::OK) {
      mutex_.Lock();
      auto ans = queue_handle_.Pop();
      mutex_.Unlock();
      return ans;
    } else {
      return ErrorCode::TIMEOUT;
    }
  }

  ErrorCode Overwrite(const Data &data) {
    mutex_.Lock();
    while (semaphore_handle_.Wait(0) != ErrorCode::OK) {
    }
    auto ans = queue_handle_.Overwrite(data);
    semaphore_handle_.Post();
    mutex_.Unlock();
    return ans;
  }

  ErrorCode PushFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Push(data);
  }

  ErrorCode PopFromCallback(Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Pop(data, 0);
  }

  ErrorCode OverwriteFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Overwrite(data);
  }

  ErrorCode Peek(Data &item) {
    mutex_.Lock();
    auto ans = queue_handle_.Peek(item);
    mutex_.Unlock();
    return ans;
  }

  ErrorCode PeekFromCallback(Data &item, bool in_isr) {
    UNUSED(in_isr);
    return Peek(item);
  }

  void Reset() {
    mutex_.Lock();
    while (semaphore_handle_.Wait(0) == ErrorCode::OK) {
    };
    queue_handle_.Reset();
    mutex_.Unlock();
  }

  size_t Size() {
    mutex_.Lock();
    auto ans = queue_handle_.Size();
    mutex_.Unlock();
    return ans;
  }

  size_t EmptySize() {
    mutex_.Lock();
    auto ans = queue_handle_.EmptySize();
    mutex_.Unlock();
    return ans;
  }

  ErrorCode PopBatch(Data *data, size_t size, uint32_t timeout) {
    auto now = Timebase::GetMilliseconds();
    do {
      mutex_.Lock();
      auto ans = queue_handle_.PopBatch(data, size);
      mutex_.Unlock();
      if (ans == ErrorCode::OK) {
        return ans;
      } else {
        semaphore_handle_.Wait(timeout - (Timebase::GetMilliseconds() - now));
      }
    } while (Timebase::GetMilliseconds() - now < timeout);

    return ErrorCode::TIMEOUT;
  }

  ErrorCode PushBatch(const Data *data, size_t size) {
    mutex_.Lock();
    auto ans = queue_handle_.PushBatch(data, size);
    mutex_.Unlock();
    return ans;
  }

  ErrorCode PushBatchFromCallback(const Data *data, size_t size, bool in_isr) {
    UNUSED(in_isr);
    return PushBatch(data, size);
  }

  ErrorCode PopBatchFromCallback(Data *data, size_t size, bool in_isr) {
    UNUSED(in_isr);
    return PopBatch(data, size, 0);
  }

 private:
  Queue<Data> queue_handle_;
  Mutex mutex_;
  Semaphore semaphore_handle_;
};
}  // namespace LibXR
