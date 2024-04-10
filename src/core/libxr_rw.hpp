#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR {

template <typename... Args> class OperationBlock {
  union {
    uint32_t timeout;
    Callback<void, Args...> callback;
  } Data;

  bool sync;

  OperationBlock(uint32_t timeout) {
    sync = true;
    Data = timeout;
  }

  OperationBlock(Callback<void, Args...> callback) {
    sync = true;
    Data = callback;
  }
};

template <typename... Args> class Operation {
public:
  Operation(uint32_t timeout) { block_ = new OperationBlock<Args...>(timeout); }
  Operation(Callback<void, Args...> callback) {
    block_ = new OperationBlock(callback);
  }

private:
  OperationBlock<Args...> *block_;
};

typedef ErrorCode (*WriteFunction)(const RawData &data,
                                   Operation<ErrorCode> &op);
typedef ErrorCode (*ReadFunction)(RawData &data,
                                  Operation<ErrorCode, const RawData &> &op);

class ReadPort {
public:
  ReadFunction read;
};

class WritePort {
public:
  WriteFunction write;
};

class STDIO {
public:
  static ReadFunction read;
  static WriteFunction write;
};
} // namespace LibXR