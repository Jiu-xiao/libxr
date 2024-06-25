#pragma once

#include "mutex.hpp"

namespace LibXR {
template <typename Data, uint32_t Depth> class Stack {
private:
  std::array<Data, Depth> stack_;
  uint32_t top;
  LibXR::Mutex mutex_;

public:
  Stack() : top(0) {}

  bool Empty() const { return (top == 0); }

  ErrorCode Push(Data const &data) {
    mutex_.Lock();

    if (top >= Depth) {
      mutex_.Unlock();
      return ErrorCode::FULL;
    }
    stack_[top++] = data;
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  ErrorCode Pop(Data &data) {
    mutex_.Lock();

    if (top == 0) {
      mutex_.Unlock();
      return ErrorCode::EMPTY;
    }
    data = stack_[--top];
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  ErrorCode Peek(Data &data) {
    mutex_.Lock();

    if (top == 0) {
      mutex_.Unlock();

      return ErrorCode::EMPTY;
    }
    data = stack_[top - 1];
    mutex_.Unlock();

    return ErrorCode::OK;
  }
};
} // namespace LibXR