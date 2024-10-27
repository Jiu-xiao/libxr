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
template <typename... Args>
class Operation {
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

  Operation(Operation &op) {
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

  void UpdateStatus(bool in_isr, LibXR::Semaphore &sem, Args &&...args) {
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

  void UpdateStatus() {
    if (type == OperationType::POLLING) {
      data.status = OperationPollingStatus::RUNNING;
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

typedef struct {
  RawData data;
  ReadOperation op;
} ReadInfoBlock;

typedef struct {
  ConstRawData data;
  WriteOperation op;
} WriteInfoBlock;

class ReadPort {
 public:
  ReadFun read_fun_ = nullptr;
  Semaphore *read_sem_;
  LockFreeQueue<ReadInfoBlock> *queue_;
  ReadInfoBlock info_;

  ReadPort(size_t queue_size = 3)
      : read_sem_(new Semaphore()),
        queue_(new LockFreeQueue<ReadInfoBlock>(queue_size)) {}

  size_t EmptySize() { return queue_->EmptySize(); }
  size_t Size() { return queue_->Size(); }

  bool Readable() { return read_fun_ != nullptr; }

  ReadPort &operator=(ReadFun fun) {
    read_fun_ = fun;
    return *this;
  }

  void UpdateStatus(bool in_isr, ErrorCode ans) {
    info_.op.UpdateStatus(in_isr, *read_sem_, std::forward<ErrorCode>(ans),
                          info_.data);
  }

  void UpdateStatus() { info_.op.UpdateStatus(); }

  template <typename ReadOperation>
  ErrorCode operator()(RawData data, ReadOperation &&op) {
    if (Readable()) {
      info_.op = op;
      info_.data = data;
      return read_fun_(info_.op, info_.data, *this);
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode operator()(RawData data) {
    static auto default_op = ReadOperation();
    return operator()(data, default_op);
  }

  ReadOperation::OperationPollingStatus GetStatus() {
    if (info_.op.data.status == ReadOperation::OperationPollingStatus::DONE) {
      info_.op.data.status = ReadOperation::OperationPollingStatus::READY;
      return ReadOperation::OperationPollingStatus::DONE;
    } else {
      return info_.op.data.status;
    }
  }
};

class WritePort {
 public:
  WriteFun write_fun_ = nullptr;
  Semaphore *write_sem_;
  LockFreeQueue<WriteInfoBlock> *queue_;
  WriteInfoBlock info_;

  WritePort(size_t queue_size = 3)
      : write_sem_(new Semaphore()),
        queue_(new LockFreeQueue<WriteInfoBlock>(queue_size)) {}

  size_t EmptySize() { return queue_->EmptySize(); }
  size_t Size() { return queue_->Size(); }

  bool Writable() { return write_fun_ != nullptr; }

  WritePort &operator=(WriteFun fun) {
    write_fun_ = fun;
    return *this;
  }

  void UpdateStatus(bool in_isr, ErrorCode ans) {
    info_.op.UpdateStatus(in_isr, *write_sem_, std::forward<ErrorCode>(ans));
  }

  void UpdateStatus() { info_.op.UpdateStatus(); }

  ErrorCode operator()(ConstRawData data, WriteOperation &op) {
    return operator()(data, std::forward<WriteOperation>(op));
  }

  template <typename WriteOperation>
  ErrorCode operator()(ConstRawData data, WriteOperation &&op) {
    if (Writable()) {
      info_.op = op;
      info_.data = data;
      return write_fun_(info_.op, info_.data, *this);
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode operator()(ConstRawData data) {
    static auto default_op = WriteOperation();
    return operator()(data, default_op);
  }

  WriteOperation::OperationPollingStatus GetStatus() {
    if (info_.op.data.status == WriteOperation::OperationPollingStatus::DONE) {
      info_.op.data.status = WriteOperation::OperationPollingStatus::READY;
      return WriteOperation::OperationPollingStatus::DONE;
    } else {
      return info_.op.data.status;
    }
  }
};

class STDIO {
 public:
  static ReadPort *read;
  static WritePort *write;
};
}  // namespace LibXR