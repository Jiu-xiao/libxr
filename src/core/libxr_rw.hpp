#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "semaphore.hpp"

namespace LibXR {

template <typename... Args> class Operation {
public:
  enum class OperationType { CALLBACK, BLOCK, POLLING };

  enum class OperationPollingStatus { READY, RUNNING, DONE };

  Operation() {
    data.status = OperationPollingStatus::READY;
    type = OperationType::POLLING;
  }

  void operator=(Operation &op) {
    type = op.type;
    switch (type) {
    case OperationType::BLOCK:
      data.callback = op.data.callback;
      break;
    case OperationType::CALLBACK:
      data.timeout = op.data.timeout;
      break;
    case OperationType::POLLING:
      data.status = op.data.status;
      break;
    }
  }

  Operation(uint32_t timeout) {
    data.timeout = timeout;
    type = OperationType::BLOCK;
  }

  Operation(Callback<Args...> callback) {
    data.callback = callback;
    type = OperationType::CALLBACK;
  }

  Operation(Operation &op) { memcpy(this, &op, sizeof(op)); }

  union {
    Callback<Args...> callback = Callback<Args...>();
    uint32_t timeout;
    OperationPollingStatus status;
  } data;

  OperationType type;
};

typedef Operation<ErrorCode, RawData &> ReadOperation;
typedef Operation<ErrorCode> WriteOperation;

typedef ErrorCode (*WritePort)(WriteOperation &op, ConstRawData data);

typedef ErrorCode (*ReadPort)(ReadOperation &op, RawData data);

class STDIO {
public:
  static ReadPort read;
  static WritePort write;
  static void (*error)(const char *log);
};
} // namespace LibXR