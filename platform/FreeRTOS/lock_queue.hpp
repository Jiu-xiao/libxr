#pragma once

#include <array>
#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "mutex.hpp"
#include "queue.hpp"
#include "semaphore.hpp"

namespace LibXR {
template <typename Data> class LockQueue {
public:
  LockQueue(size_t length)
      : queue_handle_(xQueueCreate(length, sizeof(Data))), length_(length) {}

  ~LockQueue() { vQueueDelete(queue_handle_); }

  ErrorCode Push(const Data &data) {
    auto ans = xQueueSend(queue_handle_, &data, 0);
    if (ans == pdTRUE) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::FULL;
    }
  }

  ErrorCode Pop(Data &data, uint32_t timeout) {
    auto ans = xQueueReceive(queue_handle_, &data, timeout);
    if (ans == pdTRUE) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Pop(uint32_t timeout) {
    Data data;
    auto ans = xQueueReceive(queue_handle_, &data, timeout);
    if (ans == pdTRUE) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode Overwrite(const Data &data) {
    xQueueReset(queue_handle_);
    return Push(data);
  }

  ErrorCode PushFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Push(data);
  }

  ErrorCode OverwriteFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Overwrite(data);
  }

  void Reset() { xQueueReset(queue_handle_); }

  size_t Size() { return length_ - EmptySize(); }

  size_t EmptySize() { return uxQueueSpacesAvailable(queue_handle_); }

private:
  QueueHandle_t queue_handle_;
  uint32_t length_;
};
} // namespace LibXR
