#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

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
    case OperationType::CALLBACK:
      data.callback = op.data.callback;
      break;
    case OperationType::BLOCK:
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

  void Update(bool in_isr, LibXR::Semaphore &sem, Args &&...args) {
    switch (type) {
    case OperationType::CALLBACK:
      data.callback.Run(in_isr, std::forward<Args>(args)...);
      break;
    case OperationType::BLOCK:
      sem.PostFromCallback(in_isr);
      break;
    case OperationType::POLLING:
      data.status = OperationPollingStatus::DONE;
      break;
    }
  }

  union {
    Callback<Args...> callback = Callback<Args...>();
    uint32_t timeout;
    OperationPollingStatus status;
  } data;

  OperationType type;
};

class ReadPort;
class WritePort;

typedef Operation<ErrorCode, RawData &> ReadOperation;
typedef Operation<ErrorCode> WriteOperation;

typedef ErrorCode (*WriteFun)(WriteOperation &op, ConstRawData data,
                              WritePort &port);

typedef ErrorCode (*ReadFun)(ReadOperation &op, RawData data, ReadPort &port);

class ReadPort {
public:
  ReadFun read_fun_ = nullptr;
  ReadOperation read_op_;
  Semaphore read_sem_;
  RawData data_;

  bool Readable() { return read_fun_ != nullptr; }

  ReadPort &operator=(ReadFun fun) {
    read_fun_ = fun;
    return *this;
  }

  void Update(bool in_isr, ErrorCode ans) {
    read_op_.Update(in_isr, read_sem_, std::forward<ErrorCode>(ans), data_);
  }

  ErrorCode operator()(ReadOperation &op, RawData &data) {
    if (Readable()) {
      read_op_ = op;
      data_ = data;
      return read_fun_(read_op_, data_, *this);
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }
};

class WritePort {
public:
  WriteFun write_fun_ = nullptr;
  WriteOperation write_op_;
  Semaphore write_sem_;
  ConstRawData data_;

  bool Writable() { return write_fun_ != nullptr; }

  WritePort &operator=(WriteFun fun) {
    write_fun_ = fun;
    return *this;
  }

  void Update(bool in_isr, ErrorCode ans) {
    write_op_.Update(in_isr, write_sem_, std::forward<ErrorCode>(ans));
  }

  ErrorCode operator()(WriteOperation &op, ConstRawData data) {
    if (Writable()) {
      write_op_ = op;
      data_ = data;
      return write_fun_(write_op_, data_, *this);
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }
};

class STDIO {
public:
  static ReadPort read;
  static WritePort write;
  static void (*error)(const char *log);
};
} // namespace LibXR