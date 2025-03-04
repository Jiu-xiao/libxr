#pragma once

#include "libxr_def.hpp"
#include "mutex.hpp"

namespace LibXR {
template <typename Data> class Stack {
private:
  Data *stack_;
  uint32_t top_;
  uint32_t depth_;
  LibXR::Mutex mutex_;

public:
  Stack(uint32_t depth) : stack_(new Data[depth]), top_(0), depth_(depth) {}

  Data *operator&() {
    if (Size() > 0) {
      return stack_;
    } else {
      return nullptr;
    }
  }

  Data &operator[](int32_t index) {
    if (index >= 0) {
      ASSERT(index < depth_);
      return stack_[index];
    } else {
      ASSERT(depth_ + index >= 0);
      return stack_[top_ + index];
    }
  }

  uint32_t Size() const { return top_; }

  uint32_t EmptySize() const { return (depth_ - top_); }

  ErrorCode Push(const Data &data) {
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

  ErrorCode Insert(const Data &data, uint32_t index) {
    mutex_.Lock();
    if (top_ >= depth_) {
      mutex_.Unlock();
      return ErrorCode::FULL;
    }

    if (index > top_) {
      mutex_.Unlock();
      return ErrorCode::OUT_OF_RANGE;
    }

    for (uint32_t i = top_ + 1; i > index; i--) {
      stack_[i] = stack_[i - 1];
    }

    stack_[index] = data;
    top_++;

    mutex_.Unlock();

    return ErrorCode::OK;
  }

  ErrorCode Delete(uint32_t index) {
    mutex_.Lock();
    if (index >= top_) {
      mutex_.Unlock();
      return ErrorCode::OUT_OF_RANGE;
    }

    for (uint32_t i = index; i < top_ - 1; i++) {
      stack_[i] = stack_[i + 1];
    }
    top_--;
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