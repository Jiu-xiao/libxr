#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "chunk_queue.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lock_queue.hpp"
#include "lockfree_queue.hpp"
#include "semaphore.hpp"

namespace LibXR
{

/**
 * @brief Defines an operation with different execution modes.
 * @brief 定义了一种具有不同执行模式的操作。
 *
 * @tparam Args The parameter types for callback operations.
 * @tparam Args 用于回调操作的参数类型。
 */
template <typename... Args>
class Operation
{
 public:
  /// Operation types.
  /// 操作类型。
  enum class OperationType : uint8_t
  {
    CALLBACK,
    BLOCK,
    POLLING,
    NONE
  };

  /// Polling operation status.
  /// 轮询操作的状态。
  enum class OperationPollingStatus : uint8_t
  {
    READY,
    RUNNING,
    DONE
  };

  /// @brief Default constructor, initializes with NONE type.
  /// @brief 默认构造函数，初始化为NONE类型。
  Operation() : type(OperationType::NONE) {}

  /**
   * @brief Constructs a blocking operation with a semaphore and timeout.
   * @brief 使用信号量和超时构造阻塞操作。
   * @param sem Semaphore reference.
   * @param timeout Timeout duration (default is maximum).
   */
  Operation(Semaphore &sem, uint32_t timeout = UINT32_MAX) : type(OperationType::BLOCK)
  {
    data.sem_info.sem = &sem;
    data.sem_info.timeout = timeout;
  }

  /**
   * @brief Constructs a callback-based operation.
   * @brief 构造基于回调的操作。
   * @param callback Callback function reference.
   */
  Operation(Callback<Args...> &callback) : type(OperationType::CALLBACK)
  {
    data.callback = &callback;
  }

  /**
   * @brief Constructs a polling operation.
   * @brief 构造轮询操作。
   * @param status Reference to polling status.
   */
  Operation(OperationPollingStatus &status) : type(OperationType::POLLING)
  {
    data.status = &status;
  }

  /**
   * @brief Copy assignment operator.
   * @brief 复制赋值运算符。
   * @param op Another Operation instance.
   * @return Reference to this operation.
   */
  Operation &operator=(const Operation &op)
  {
    if (this != &op)
    {
      type = op.type;
      switch (type)
      {
        case OperationType::CALLBACK:
          data.callback = op.data.callback;
          break;
        case OperationType::BLOCK:
          data.sem_info.sem = op.data.sem_info.sem;
          data.sem_info.timeout = op.data.sem_info.timeout;
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

  /**
   * @brief Move assignment operator.
   * @brief 移动赋值运算符。
   * @param op Another Operation instance.
   * @return Reference to this operation.
   */
  Operation &operator=(Operation &&op) noexcept
  {
    if (this != &op)
    {
      type = op.type;
      switch (type)
      {
        case OperationType::CALLBACK:
          data.callback = op.data.callback;
          break;
        case OperationType::BLOCK:
          data.sem_info.sem = op.data.sem_info.sem;
          data.sem_info.timeout = op.data.sem_info.timeout;
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

  Operation(const Operation &op) : type(op.type)
  {
    switch (type)
    {
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

  /**
   * @brief 构造一个新的 Operation 对象（移动构造函数）。
   *        Constructs a new Operation object (move constructor).
   *
   * 该构造函数用于移动另一个 Operation 对象，并接管其内部数据。
   * This constructor moves another Operation object and takes ownership of its internal
   * data.
   *
   * @param op 要移动的 Operation 对象。
   *           The Operation object to be moved.
   *
   * @note 该操作不会动态分配内存，仅仅是移动已有数据成员。
   *       This operation does not allocate memory dynamically; it merely moves existing
   * data members.
   */
  Operation(Operation &&op) noexcept : type(op.type)
  {
    switch (type)
    {
      case OperationType::CALLBACK:
        data.callback = op.data.callback;
        break;
      case OperationType::BLOCK:
        data.sem_info.sem = op.data.sem_info.sem;
        data.sem_info.timeout = op.data.sem_info.timeout;
        break;
      case OperationType::POLLING:
        data.status = op.data.status;
        break;
      case OperationType::NONE:
        ASSERT(false);
    }
  }

  /**
   * @brief Updates operation status based on type.
   * @brief 根据类型更新操作状态。
   * @param in_isr Indicates if executed within an interrupt.
   * @param args Parameters passed to the callback.
   */
  void UpdateStatus(bool in_isr, Args &&...args)
  {
    switch (type)
    {
      case OperationType::CALLBACK:
        data.callback->Run(in_isr, std::forward<Args>(args)...);
        break;
      case OperationType::BLOCK:
        data.sem_info.sem->PostFromCallback(in_isr);
        break;
      case OperationType::POLLING:
        *data.status = OperationPollingStatus::DONE;
        break;
      case OperationType::NONE:
        ASSERT(false);
    }
  }

  /**
   * @brief 标记操作为运行状态。
   *        Marks the operation as running.
   *
   * 该函数用于在操作类型为 `POLLING` 时，将 `data.status` 设置为 `RUNNING`，
   * 以指示该操作正在执行中。
   * This function sets `data.status` to `RUNNING` when the operation type is `POLLING`,
   * indicating that the operation is currently in progress.
   *
   * @note 该方法仅适用于 `OperationType::POLLING` 类型的操作，其他类型不会受到影响。
   *       This method only applies to operations of type `OperationType::POLLING`,
   *       and other types remain unaffected.
   */
  void MarkAsRunning()
  {
    if (type == OperationType::POLLING)
    {
      *data.status = OperationPollingStatus::RUNNING;
    }
  }

  /// Data storage for different operation types.
  /// 存储不同操作类型的数据。
  union
  {
    Callback<Args...> *callback;
    struct
    {
      Semaphore *sem;
      uint32_t timeout;
    } sem_info;
    OperationPollingStatus *status;
  } data;

  /// Operation type.
  /// 操作类型。
  OperationType type;
};

class ReadPort;
class WritePort;

/// @brief Read operation type.
/// @brief 读取操作类型。
typedef Operation<ErrorCode> ReadOperation;

/// @brief Write operation type.
/// @brief 写入操作类型。
typedef Operation<ErrorCode> WriteOperation;

/// @brief Function pointer type for write operations.
/// @brief 写入操作的函数指针类型。
typedef ErrorCode (*WriteFun)(WritePort &port);

/// @brief Function pointer type for read operations.
/// @brief 读取操作的函数指针类型。
typedef ErrorCode (*ReadFun)(ReadPort &port);

/**
 * @brief Read information block structure.
 * @brief 读取信息块结构。
 */
class ReadInfoBlock
{
 public:
  RawData data_;      ///< Data buffer. 数据缓冲区。
  ReadOperation op_;  ///< Read operation instance. 读取操作实例。

  /**
   * @brief Constructs a ReadInfoBlock.
   * @brief 构造一个ReadInfoBlock。
   * @param data Data buffer.
   * @param op Read operation instance.
   */
  ReadInfoBlock(RawData data, ReadOperation &&op) : data_(data), op_(std::move(op)) {}

  /// @brief Default constructor.
  /// @brief 默认构造函数。
  ReadInfoBlock() : data_(), op_() {}
};

/**
 * @brief ReadPort class for handling read operations.
 * @brief 处理读取操作的ReadPort类。
 */
class ReadPort
{
 public:
  ReadFun read_fun_ = nullptr;
  LockFreeQueue<ReadInfoBlock> *queue_block_ = nullptr;
  BaseQueue *queue_data_ = nullptr;
  size_t read_size_ = 0;

  /**
   * @brief Constructs a ReadPort with queue sizes.
   * @brief 以指定队列大小构造ReadPort。
   * @param queue_size Number of queued operations.
   * @param buffer_size Size of each buffer.
   */
  ReadPort(size_t queue_size = 3, size_t buffer_size = 128)
      : queue_block_(new LockFreeQueue<ReadInfoBlock>(queue_size)),
        queue_data_(new BaseQueue(1, buffer_size))
  {
  }

  /**
   * @brief 获取队列的剩余可用空间。
   *        Gets the remaining available space in the queue.
   *
   * 该函数返回 `queue_block_` 中当前可用的空闲空间大小。
   * This function returns the size of the available empty space in `queue_block_`.
   *
   * @return 返回队列的空闲大小（单位：字节）。
   *         Returns the empty size of the queue (in bytes).
   */
  size_t EmptySize() { return queue_block_->EmptySize(); }

  /**
   * @brief 获取当前队列的已使用大小。
   *        Gets the currently used size of the queue.
   *
   * 该函数返回 `queue_block_` 当前已占用的空间大小。
   * This function returns the size of the space currently used in `queue_block_`.
   *
   * @return 返回队列的已使用大小（单位：字节）。
   *         Returns the used size of the queue (in bytes).
   */
  size_t Size() { return queue_block_->Size(); }

  /// @brief Checks if read operations are supported.
  /// @brief 检查是否支持读取操作。
  bool Readable() { return read_fun_ != nullptr; }

  /**
   * @brief 赋值运算符重载，用于设置读取函数。
   *        Overloaded assignment operator to set the read function.
   *
   * 该函数允许使用 `ReadFun` 类型的函数对象赋值给 `ReadPort`，从而设置 `read_fun_`。
   * This function allows assigning a `ReadFun` function object to `ReadPort`, setting
   * `read_fun_`.
   *
   * @param fun 要分配的读取函数。
   *            The read function to be assigned.
   * @return 返回自身的引用，以支持链式调用。
   *         Returns a reference to itself for chaining.
   */
  ReadPort &operator=(ReadFun fun)
  {
    read_fun_ = fun;
    return *this;
  }

  /**
   * @brief 更新读取操作的状态。
   *        Updates the status of the read operation.
   *
   * 该函数用于在读取操作过程中更新 `read_size_` 并调用 `UpdateStatus` 方法更新 `info.op_`
   * 的状态。 This function updates `read_size_` and calls `UpdateStatus` on `info.op_`
   * during a read operation.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @param ans 错误码，用于指示操作的结果。
   *            Error code indicating the result of the operation.
   * @param info 需要更新状态的 `ReadInfoBlock` 引用。
   *             Reference to the `ReadInfoBlock` whose status needs to be updated.
   * @param size 读取的数据大小。
   *             The size of the read data.
   */
  void UpdateStatus(bool in_isr, ErrorCode ans, ReadInfoBlock &info, uint32_t size)
  {
    read_size_ = size;
    info.op_.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
  }

  /**
   * @brief 标记读取操作为运行中。
   *        Marks the read operation as running.
   *
   * 该函数用于将 `info.op_` 标记为运行状态，以指示当前正在进行读取操作。
   * This function marks `info.op_` as running to indicate an ongoing read operation.
   *
   * @param info 需要更新状态的 `ReadInfoBlock` 引用。
   *             Reference to the `ReadInfoBlock` whose status needs to be updated.
   */
  void UpdateStatus(ReadInfoBlock &info) { info.op_.MarkAsRunning(); }

  /**
   * @brief 读取操作符重载，用于执行读取操作。
   *        Overloaded function call operator to perform a read operation.
   *
   * 该函数检查端口是否可读，并根据 `data.size_` 和 `op` 的类型执行不同的操作。
   * This function checks if the port is readable and performs different actions based on
   * `data.size_` and the type of `op`.
   *
   * @param data 包含要读取的数据。
   *             Contains the data to be read.
   * @param op 读取操作对象，包含操作类型和同步机制。
   *           Read operation object containing the operation type and synchronization
   * mechanism.
   * @return 返回操作的 `ErrorCode`，指示操作结果。
   *         Returns an `ErrorCode` indicating the result of the operation.
   */
  ErrorCode operator()(RawData data, ReadOperation &op)
  {
    if (Readable())
    {
      if (data.size_ == 0)
      {
        op.UpdateStatus(false, ErrorCode::OK);
        if (op.type == ReadOperation::OperationType::BLOCK)
        {
          return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
        }
        return ErrorCode::OK;
      }

      ReadInfoBlock block = {data, std::move(op)};
      if (queue_block_->Push(block) != ErrorCode::OK)
      {
        return ErrorCode::FULL;
      }

      auto ans = read_fun_(*this);
      if (op.type == ReadOperation::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }
      else
      {
        return ans;
      }
    }
    else
    {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  /**
   * @brief Processes pending reads.
   * @brief 处理挂起的读取请求。
   */
  void ProcessPendingReads()
  {
    ReadInfoBlock block;
    while (queue_block_->Peek(block) == ErrorCode::OK)
    {
      if (queue_data_->Size() >= block.data_.size_)
      {
        queue_data_->PopBatch(block.data_.addr_, block.data_.size_);
        read_size_ = block.data_.size_;
        block.op_.UpdateStatus(true, ErrorCode::OK);
        queue_block_->Pop();
      }
      else
      {
        break;
      }
    }
  }

  /// @brief Resets the ReadPort.
  /// @brief 重置ReadPort。
  void Reset()
  {
    queue_block_->Reset();
    queue_data_->Reset();
    read_size_ = 0;
  }
};

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  WriteFun write_fun_ = nullptr;
  LockFreeQueue<WriteOperation> *queue_op_;
  ChunkQueue *queue_data_;
  size_t write_size_ = 0;

  /**
   * @brief 构造一个新的 WritePort 对象。
   *        Constructs a new WritePort object.
   *
   * 该构造函数初始化无锁操作队列 `queue_op_` 和数据块队列 `queue_data_`。
   * This constructor initializes the lock-free operation queue `queue_op_` and the data
   * block queue `queue_data_`.
   *
   * @param queue_size 队列的大小，默认为 3。
   *                   The size of the queue, default is 3.
   * @param block_size 每个数据块的大小，默认为 128。
   *                   The size of each data block, default is 128.
   */
  WritePort(size_t queue_size = 3, size_t block_size = 128)
      : queue_op_(new LockFreeQueue<WriteOperation>(queue_size)),
        queue_data_(new ChunkQueue(queue_size, block_size))
  {
  }

  /**
   * @brief 获取数据队列的剩余可用空间。
   *        Gets the remaining available space in the data queue.
   *
   * @return 返回数据队列的空闲大小。
   *         Returns the empty size of the data queue.
   */
  size_t EmptySize() { return queue_data_->EmptySize(); }

  /**
   * @brief 获取当前数据队列的已使用大小。
   *        Gets the used size of the current data queue.
   *
   * @return 返回数据队列的已使用大小。
   *         Returns the size of the data queue.
   */
  size_t Size() { return queue_data_->Size(); }

  /**
   * @brief 判断端口是否可写。
   *        Checks whether the port is writable.
   *
   * @return 如果 `write_fun_` 不为空，则返回 `true`，否则返回 `false`。
   *         Returns `true` if `write_fun_` is not null, otherwise returns `false`.
   */
  bool Writable() { return write_fun_ != nullptr; }

  /**
   * @brief 赋值运算符重载，用于设置写入函数。
   *        Overloaded assignment operator to set the write function.
   *
   * 该函数允许使用 `WriteFun` 类型的函数对象赋值给 `WritePort`，从而设置 `write_fun_`。
   * This function allows assigning a `WriteFun` function object to `WritePort`, setting
   * `write_fun_`.
   *
   * @param fun 要分配的写入函数。
   *            The write function to be assigned.
   * @return 返回自身的引用，以支持链式调用。
   *         Returns a reference to itself for chaining.
   */
  WritePort &operator=(WriteFun fun)
  {
    write_fun_ = fun;
    return *this;
  }

  /**
   * @brief 更新写入操作的状态。
   *        Updates the status of the write operation.
   *
   * 该函数在写入操作过程中更新 `write_size_` 并调用 `UpdateStatus` 方法更新 `op` 的状态。
   * This function updates `write_size_` and calls `UpdateStatus` on `op` during a write
   * operation.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @param ans 错误码，用于指示操作的结果。
   *            Error code indicating the result of the operation.
   * @param op 需要更新状态的 `WriteOperation` 引用。
   *           Reference to the `WriteOperation` whose status needs to be updated.
   * @param size 写入的数据大小。
   *             The size of the written data.
   */
  void UpdateStatus(bool in_isr, ErrorCode ans, WriteOperation &op, uint32_t size)
  {
    write_size_ = size;
    op.UpdateStatus(in_isr, std::forward<ErrorCode>(ans));
  }

  /**
   * @brief 标记写入操作为运行中。
   *        Marks the write operation as running.
   *
   * 该函数用于将 `op` 标记为运行状态，以指示当前正在进行写入操作。
   * This function marks `op` as running to indicate an ongoing write operation.
   *
   * @param op 需要更新状态的 `WriteOperation` 引用。
   *           Reference to the `WriteOperation` whose status needs to be updated.
   */
  void UpdateStatus(WriteOperation &op) { op.MarkAsRunning(); }

  /**
   * @brief 执行写入操作。
   *        Performs a write operation.
   *
   * 该函数检查端口是否可写，并根据 `data.size_` 和 `op` 的类型执行不同的操作。
   * This function checks if the port is writable and performs different actions based on
   * `data.size_` and the type of `op`.
   *
   * @param data 需要写入的原始数据。
   *             Raw data to be written.
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * mechanism.
   * @return 返回操作的 `ErrorCode`，指示操作结果。
   *         Returns an `ErrorCode` indicating the result of the operation.
   */
  ErrorCode operator()(ConstRawData data, WriteOperation &op)
  {
    if (Writable())
    {
      if (data.size_ == 0)
      {
        op.UpdateStatus(false, ErrorCode::OK);
        if (op.type == WriteOperation::OperationType::BLOCK)
        {
          return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
        }
        return ErrorCode::OK;
      }

      if (queue_data_->EmptySize() < data.size_ || queue_data_->EmptyBlockSize() < 1 ||
          queue_op_->EmptySize() < 1)
      {
        return ErrorCode::FULL;
      }

      queue_op_->Push(std::forward<WriteOperation>(op));
      Flush();
      queue_data_->AppendToCurrentBlock(data.addr_, data.size_);

      auto ans = write_fun_(*this);
      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }
      else
      {
        return ans;
      }
    }
    else
    {
      return ErrorCode::NOT_SUPPORT;
    }
  }

  /// @brief Flushes pending write operations.
  /// @brief 刷新挂起的写入操作。
  void Flush() { queue_data_->CreateNewBlock(); }

  /// @brief Resets the WritePort.
  /// @brief 重置WritePort。
  void Reset()
  {
    queue_op_->Reset();
    queue_data_->Reset();
    write_size_ = 0;
  }
};

/**
 * @brief STDIO interface for read/write ports.
 * @brief 提供静态全局的输入输出接口绑定与缓冲区管理。
 */
class STDIO
{
 public:
  // NOLINTBEGIN
  static ReadPort *read_;    ///< Read port instance. 读取端口。
  static WritePort *write_;  ///< Write port instance. 写入端口。
  static char printf_buff_[LIBXR_PRINTF_BUFFER_SIZE];  ///< Print buffer. 打印缓冲区。

  // NOLINTEND

  /**
   * @brief Prints a formatted string to the write port (like printf).
   * @brief 将格式化字符串发送至写入端口，类似 printf。
   *
   * @param fmt The format string. 格式化字符串。
   * @param ... Variable arguments to be formatted. 需要格式化的参数。
   * @return Number of characters written, or negative on error.
   *         成功返回写入的字符数，失败返回负数。
   */
  static int Printf(const char *fmt, ...)
  {
    if (!STDIO::write_ || !STDIO::write_->Writable())
    {
      return -1;
    }

    static LibXR::Semaphore sem;  // NOLINT

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(STDIO::printf_buff_, LIBXR_PRINTF_BUFFER_SIZE, fmt, args);
    va_end(args);

    // Check result and limit length
    if (len < 0)
    {
      return -1;
    }
    if (static_cast<size_t>(len) >= LIBXR_PRINTF_BUFFER_SIZE)
    {
      len = LIBXR_PRINTF_BUFFER_SIZE - 1;
    }

    ConstRawData data = {reinterpret_cast<const uint8_t *>(STDIO::printf_buff_),
                         static_cast<size_t>(len)};

    static WriteOperation op(sem, LIBXR_PRINTF_TIMEOUT);  // NOLINT
    return static_cast<int>(STDIO::write_->operator()(data, op));
  }
};
}  // namespace LibXR
