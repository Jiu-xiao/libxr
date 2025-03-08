#pragma once

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR {
template <typename Data>
class LockQueue {
 public:
  LockQueue(size_t length)
      : queue_handle_(xQueueCreate(length, sizeof(Data))), LENGTH(length) {}

  ~LockQueue() { vQueueDelete(queue_handle_); }

  ErrorCode Push(const Data &data) {
    auto ans = xQueueSend(queue_handle_, &data, 0);
    if (ans == pdTRUE) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::FULL;
    }
  }

  ErrorCode Pop(Data &data, uint32_t timeout = UINT32_MAX) {
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
    if (!in_isr) {
      return Push(data) == pdTRUE ? ErrorCode::OK : ErrorCode::FULL;
    } else {
      BaseType_t xHigherPriorityTaskWoken;  // NOLINT
      auto ans =
          xQueueSendFromISR(queue_handle_, &data, &xHigherPriorityTaskWoken);
      if (xHigherPriorityTaskWoken) {
        portYIELD();  // NOLINT
      }
      if (ans == pdTRUE) {
        return ErrorCode::OK;
      } else {
        return ErrorCode::FULL;
      }
    }
  }

  ErrorCode PopFromCallback(Data &data, bool in_isr) {
    if (!in_isr) {
      return Pop(data, 0);
    } else {
      BaseType_t xHigherPriorityTaskWoken;  // NOLINT
      auto ans =
          xQueueReceiveFromISR(queue_handle_, &data, &xHigherPriorityTaskWoken);
      if (xHigherPriorityTaskWoken) {
        portYIELD();  // NOLINT
      }
      if (ans == pdTRUE) {
        return ErrorCode::OK;
      } else {
        return ErrorCode::EMPTY;
      }
    }
  }

  ErrorCode PopFromCallback(bool in_isr) {
    if (!in_isr) {
      return Pop(0);
    } else {
      Data data;
      BaseType_t xHigherPriorityTaskWoken;  // NOLINT
      auto ans =
          xQueueReceiveFromISR(queue_handle_, &data, &xHigherPriorityTaskWoken);
      if (xHigherPriorityTaskWoken) {
        portYIELD();  // NOLINT
      }
      if (ans == pdTRUE) {
        return ErrorCode::OK;
      } else {
        return ErrorCode::EMPTY;
      }
    }
  }

  ErrorCode OverwriteFromCallback(const Data &data, bool in_isr) {
    UNUSED(in_isr);
    return Overwrite(data);
  }

  ErrorCode Peek(Data &data) {
    auto ans = xQueuePeek(queue_handle_, &data, 0);
    if (ans == pdTRUE) {
      return ErrorCode::OK;
    } else {
      return ErrorCode::EMPTY;
    }
  }

  ErrorCode PeekFromCallback(Data &data, bool in_isr) {
    if (!in_isr) {
      return Peek(data);
    } else {
      return xQueuePeekFromISR(queue_handle_, &data) == pdTRUE
                 ? ErrorCode::OK
                 : ErrorCode::EMPTY;
    }
  }

  void Reset() { xQueueReset(queue_handle_); }

  size_t Size() { return uxQueueMessagesWaiting(queue_handle_); }

  size_t EmptySize() { return uxQueueSpacesAvailable(queue_handle_); }

  size_t SizeFromCallback(bool in_isr) {
    if (!in_isr) {
      return Size();
    } else {
      return uxQueueMessagesWaitingFromISR(queue_handle_);
    }
  }

  size_t EmptySizeFromCallback(bool in_isr) {
    if (!in_isr) {
      return EmptySize();
    } else {
      return LENGTH - uxQueueMessagesWaitingFromISR(queue_handle_);
    }
  }

 private:
  QueueHandle_t queue_handle_;
  const uint32_t LENGTH;
};
}  // namespace LibXR
