#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "chunk_queue.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lock_queue.hpp"
#include "lockfree_queue.hpp"
#include "semaphore.hpp"

namespace LibXR {
template <typename... Args>
class Operation {
 public:
  enum class OperationType : uint8_t { CALLBACK, BLOCK, POLLING, NONE };

  enum class OperationPollingStatus : uint8_t { READY, RUNNING, DONE };

  Operation() : type(OperationType::NONE) {}

  Operation(Semaphore &sem, uint32_t timeout = UINT32_MAX)
      : type(OperationType::BLOCK) {
    data.sem = &sem;
    data.timeout = timeout;
  }

  Operation(Callback<Args...> &callback) : type(OperationType::CALLBACK) {
    data.callback = &callback;
  }

  Operation(OperationPollingStatus &status) : type(OperationType::POLLING) {
    data.status = &status;
  }

  Operation &operator=(const Operation &op) {
    if (this != &op) {
      type = op.type;
      switch (type) {
        case OperationType::CALLBACK:
          data.callback = op.data.callback;
          break;
        case OperationType::BLOCK:
          data.sem = op.data.sem;
          data.timeout = op.data.timeout;
          break;
        case OperationType::POLLING:
          data.status = op.data.status;
          break;
        case OperationType::NONE:
          ASSERT(false);
      }
    }
    return *this;
  }

  Operation &operator=(Operation &&op) noexcept {
    if (this != &op) {
      type = op.type;
      switch (type) {
        case OperationType::CALLBACK:
          data.callback = op.data.callback;
          break;
        case OperationType::BLOCK:
          data.sem = op.data.sem;
          data.timeout = op.data.timeout;
          break;
        case OperationType::POLLING:
          data.status = op.data.status;
          break;
        case OperationType::NONE:
          ASSERT(false);
      }
    }
    return *this;
  }

  Operation(const Operation &op) : type(op.type) {
    switch (type) {
      case OperationType::CALLBACK:
        data.callback = op.data.callback;
        break;
      case OperationType::BLOCK:
        data.sem = op.data.sem;
        data.timeout = op.data.timeout;
        break;
      case OperationType::POLLING:
        data.status = op.data.status;
        break;
    }
  }

  Operation(Operation &&op) noexcept : type(op.type) {
    switch (type) {
      case OperationType::CALLBACK:
        data.callback = op.data.callback;
        break;
      case OperationType::BLOCK:
        data.sem = op.data.sem;
        data.timeout = op.data.timeout;
        break;
      case OperationType::POLLING:
        data.status = op.data.status;
        break;
      case OperationType::NONE:
        ASSERT(false);
    }
  }

  void UpdateStatus(bool in_isr, Args &&...args) {
    switch (type) {
      case OperationType::CALLBACK:
        data.callback->Run(in_isr, std::forward<Args>(args)...);
        break;
      case OperationType::BLOCK:
        data.sem->PostFromCallback(in_isr);
        break;
      case OperationType::POLLING:
        *data.status = OperationPollingStatus::DONE;
        break;
      case OperationType::NONE:
        ASSERT(false);
    }
  }

  void MarkAsRunning() {
    if (type == OperationType::POLLING) {
      *data.status = OperationPollingStatus::RUNNING;
    }
  }

  union {
    Callback<Args...> *callback;
    struct {
      Semaphore *sem;
      uint32_t timeout;
    };
    OperationPollingStatus *status;
  } data;

  OperationType type;
};

class ReadPort;
class WritePort;

typedef Operation<ErrorCode> ReadOperation;
typedef Operation<ErrorCode> WriteOperation;

typedef ErrorCode (*WriteFun)(WritePort &port);

typedef ErrorCode (*ReadFun)(ReadPort &port);

class ReadInfoBlock {
 public:
  RawData data_;
  ReadOperation op_;

  ReadInfoBlock(RawData data, ReadOperation &&op)
      : data_(data), op_(std::move(op)) {}

  ReadInfoBlock() : data_(), op_() {}
};

class ReadPort {
 public:
  ReadFun read_fun_ = nullptr;
  LockFreeQueue<ReadInfoBlock> *queue_block_ = nullptr;
  BaseQueue *queue_data_ = nullptr;

  size_t read_size_ = 0;

  ReadPort(size_t queue_size = 3, size_t buffer_size = 128)
      : queue_block_(new LockFreeQueue<ReadInfoBlock>(queue_size)),
        queue_data_(new BaseQueue(1, buffer_size)) {}

  size_t EmptySize() { return queue_block_->EmptySize(); }
  size_t Size() { return queue_block_->Size(); }

  bool Readable() { return read_fun_ != nullptr; }

  ReadPort &operator=(ReadFun fun) {
    read_fun_ = fun;
    return *this;
  }

  void UpdateStatus(bool in_isr, ErrorCode ans, ReadInfoBlock &info,
                    uint32_t size) {
    read_size_ = size;
    info.op_.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
  }

  void UpdateStatus(ReadInfoBlock &info) { info.op_.MarkAsRunning(); }

  template <typename ReadOperation>
  ErrorCode operator()(RawData &data, ReadOperation &op) {
    if (Readable()) {
      ReadInfoBlock block = {data, std::move(op)};
      if (queue_block_->Push(block) != ErrorCode::OK) {
        return ErrorCode::FULL;
      }

      auto ans = read_fun_(*this);
      if (op.type == ReadOperation::OperationType::BLOCK) {
        return op.data.sem->Wait(op.data.timeout);
      } else {
        return ans;
      }
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  void Reset() {
    queue_block_->Reset();
    queue_data_->Reset();
    read_size_ = 0;
  }
};

class WritePort {
 public:
  WriteFun write_fun_ = nullptr;
  LockFreeQueue<WriteOperation> *queue_op_;
  ChunkQueue *queue_data_;

  size_t write_size_ = 0;

  WritePort(size_t queue_size = 3, size_t block_size = 128)
      : queue_op_(new LockFreeQueue<WriteOperation>(queue_size)),
        queue_data_(new ChunkQueue(queue_size, block_size)) {}

  size_t EmptySize() { return queue_data_->EmptySize(); }
  size_t Size() { return queue_data_->Size(); }

  bool Writable() { return write_fun_ != nullptr; }

  WritePort &operator=(WriteFun fun) {
    write_fun_ = fun;
    return *this;
  }

  void UpdateStatus(bool in_isr, ErrorCode ans, WriteOperation &op,
                    uint32_t size) {
    write_size_ = size;
    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
  }

  void UpdateStatus(WriteOperation &op) { op.MarkAsRunning(); }

  ErrorCode operator()(ConstRawData data, WriteOperation &op) {
    if (Writable()) {
      if (queue_data_->EmptySize() < data.size_ || queue_op_->EmptySize() < 1) {
        return ErrorCode::FULL;
      }

      queue_data_->AppendToCurrentBlock(data.addr_, data.size_);
      queue_op_->Push(std::forward<WriteOperation>(op));

      auto ans = write_fun_(*this);
      if (op.type == WriteOperation::OperationType::BLOCK) {
        return op.data.sem->Wait(op.data.timeout);
      } else {
        return ans;
      }
    } else {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  void Flush() { queue_data_->CreateNewBlock(); }

  void Reset() {
    queue_op_->Reset();
    queue_data_->Reset();
    write_size_ = 0;
  }
};

class STDIO {
 public:
  static ReadPort *read_;
  static WritePort *write_;
};
}  // namespace LibXR
