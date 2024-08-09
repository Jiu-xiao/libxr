#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lockfree_queue.hpp"
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

  Operation(uint32_t timeout) {
    data.timeout = timeout;
    type = OperationType::BLOCK;
  }

  Operation(Callback<Args...> callback) {
    data.callback = callback;
    type = OperationType::CALLBACK;
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
  Semaphore *read_sem_;
  RawData data_;
  LockFreeQueue<uint8_t> *queue_;

  ReadPort(size_t queue_size = 128)
      : read_sem_(new Semaphore()),
        queue_(new LockFreeQueue<uint8_t>(queue_size)) {}

  size_t EmptySize() { return queue_->EmptySize(); }
  size_t Size() { return queue_->Size(); }

  bool Readable() { return read_fun_ != nullptr; }

  ReadPort &operator=(ReadFun fun) {
    read_fun_ = fun;
    return *this;
  }

  void Update(bool in_isr, ErrorCode ans) {
    read_op_.Update(in_isr, *read_sem_, std::forward<ErrorCode>(ans), data_);
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

  ReadOperation::OperationPollingStatus GetStatus() {
    if (read_op_.data.status == ReadOperation::OperationPollingStatus::DONE) {
      read_op_.data.status = ReadOperation::OperationPollingStatus::READY;
      return ReadOperation::OperationPollingStatus::DONE;
    } else {
      return read_op_.data.status;
    }
  }
};

class WritePort {
public:
  WriteFun write_fun_ = nullptr;
  WriteOperation write_op_;
  Semaphore *write_sem_;
  ConstRawData data_;
  LockFreeQueue<uint8_t> *queue_;

  WritePort(size_t queue_size = 128)
      : write_sem_(new Semaphore()),
        queue_(new LockFreeQueue<uint8_t>(queue_size)) {}

  size_t EmptySize() { return queue_->EmptySize(); }
  size_t Size() { return queue_->Size(); }

  bool Writable() { return write_fun_ != nullptr; }

  WritePort &operator=(WriteFun fun) {
    write_fun_ = fun;
    return *this;
  }

  void Update(bool in_isr, ErrorCode ans) {
    write_op_.Update(in_isr, *write_sem_, std::forward<ErrorCode>(ans));
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

  WriteOperation::OperationPollingStatus GetStatus() {
    if (write_op_.data.status == WriteOperation::OperationPollingStatus::DONE) {
      write_op_.data.status = WriteOperation::OperationPollingStatus::READY;
      return WriteOperation::OperationPollingStatus::DONE;
    } else {
      return write_op_.data.status;
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