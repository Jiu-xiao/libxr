#pragma once

#include "libxr_def.hpp"
#include "mutex.hpp"
#include <cstdint>
#include <sys/types.h>

namespace LibXR {
template <typename Data> class Stack {
private:
  Data *stack_;
  uint32_t top_;
  uint32_t depth_;
  LibXR::Mutex mutex_;

public:
  Stack(uint32_t depth) : stack_(new Data[depth]), depth_(depth), top_(0) {}

  Data *operator&() {
    if (Size() > 0) {
      return stack_;
    } else {
      return NULL;
    }
  }

  Data operator[](uint32_t index) {
    ASSERT(index < top_);
    return stack_[index];
  }

  uint32_t Size() const { return top_; }

  uint32_t EmptySize() const { return (depth_ - top_); }

  ErrorCode Push(Data const &data) {
    mutex_.Lock();

    if (top_ >= depth_) {
      mutex_.Unlock();
      return ErrorCode::FULL;
    }
    stack_[top_++] = data;
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  ErrorCode Pop(Data &data) {
    mutex_.Lock();

    if (top_ == 0) {
      mutex_.Unlock();
      return ErrorCode::EMPTY;
    }
    data = stack_[--top_];
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  ErrorCode Pop() {
    mutex_.Lock();

    if (top_ == 0) {
      mutex_.Unlock();
      return ErrorCode::EMPTY;
    }
    --top_;
    mutex_.Unlock();
    return ErrorCode::OK;
  }

  ErrorCode Peek(Data &data) {
    mutex_.Lock();

    if (top_ == 0) {
      mutex_.Unlock();

      return ErrorCode::EMPTY;
    }
    data = stack_[top_ - 1];
    mutex_.Unlock();

    return ErrorCode::OK;
  }

  void Reset() {
    mutex_.Lock();
    top_ = 0;
    mutex_.Unlock();
  }
};
} // namespace LibXR