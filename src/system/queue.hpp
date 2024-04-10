#pragma once

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_platform.hpp"

namespace LibXR {
template <typename Data, unsigned int Length> class Queue {
public:
  Queue();
  ErrorCode Push(const Data &data);
  ErrorCode Pop(Data &data, uint32_t timeout);
  ErrorCode Overwrite(const Data &data);

  ErrorCode PushFromCallback(const Data &data, bool in_isr);
  ErrorCode OverwriteFromCallback(const Data &data, bool in_isr);

  void Reset();
  size_t Size();
  size_t EmptySize();

private:
  libxr_queue_handle queue_handle_;
};
} // namespace LibXR