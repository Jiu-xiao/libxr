#pragma once

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "lockfree_queue.hpp"
#include "mutex.hpp"
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
  using Callback = LibXR::Callback<Args...>;

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
  Operation() : type(OperationType::NONE) { Memory::FastSet(&data, 0, sizeof(data)); }

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
  Operation(Callback &callback) : type(OperationType::CALLBACK)
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
          break;
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
          break;
      }
    }
    return *this;
  }

  /**
   * @brief 构造一个新的 Operation 对象（初始化操作）。
   *        Constructs a new Operation object (initialization operation).
   *
   * 该构造函数用于初始化一个 Operation 对象，接收一个初始化操作作为参数。
   * This constructor initializes an Operation object with an initialization operation as
   * a parameter.
   */
  template <
      typename InitOperation,
      typename = std::enable_if_t<std::is_same_v<std::decay_t<InitOperation>, Operation>>>
  Operation(InitOperation &&op)
  {
    *this = std::forward<InitOperation>(op);
  }

  /**
   * @brief Updates operation status based on type.
   * @brief 根据类型更新操作状态。
   * @param in_isr Indicates if executed within an interrupt.
   * @param args Parameters passed to the callback.
   */
  template <typename... Status>
  void UpdateStatus(bool in_isr, Status &&...status)
  {
    switch (type)
    {
      case OperationType::CALLBACK:
        data.callback->Run(in_isr, std::forward<Args>(status)...);
        break;
      case OperationType::BLOCK:
        data.sem_info.sem->PostFromCallback(in_isr);
        break;
      case OperationType::POLLING:
        *data.status = OperationPollingStatus::DONE;
        break;
      case OperationType::NONE:
        break;
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
    Callback *callback;
    struct
    {
      Semaphore *sem;
      uint32_t timeout;
    } sem_info;
    OperationPollingStatus *status;
    // TODO: state
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
typedef struct
{
  RawData data;      ///< Data buffer. 数据缓冲区。
  ReadOperation op;  ///< Read operation instance. 读取操作实例。
} ReadInfoBlock;

typedef struct
{
  ConstRawData data;  ///< Data buffer. 数据缓冲区。
  WriteOperation op;  ///< Write operation instance. 写入操作实例。
} WriteInfoBlock;

/**
 * @brief ReadPort class for handling read operations.
 * @brief 处理读取操作的ReadPort类。
 */
class ReadPort
{
 public:
  enum class BusyState : uint32_t
  {
    IDLE = 0,
    PENDING = 1,
    EVENT = UINT32_MAX
  };

  ReadFun read_fun_ = nullptr;
  LockFreeQueue<uint8_t> *queue_data_ = nullptr;
  size_t read_size_ = 0;
  ReadInfoBlock info_;
  std::atomic<BusyState> busy_{BusyState::IDLE};

  /**
   * @brief Constructs a ReadPort with queue sizes.
   * @brief 以指定队列大小构造ReadPort。
   * @param queue_size Number of queued operations.
   * @param buffer_size Size of each buffer.
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  ReadPort(size_t buffer_size = 128);

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
  virtual size_t EmptySize();

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
  virtual size_t Size();

  /// @brief Checks if read operations are supported.
  /// @brief 检查是否支持读取操作。
  bool Readable();

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
  ReadPort &operator=(ReadFun fun);

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
  void Finish(bool in_isr, ErrorCode ans, ReadInfoBlock &info, uint32_t size);

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
  void MarkAsRunning(ReadInfoBlock &info);

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
  ErrorCode operator()(RawData data, ReadOperation &op);

  /**
   * @brief Processes pending reads.
   * @brief 处理挂起的读取请求。
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   */
  virtual void ProcessPendingReads(bool in_isr);

  /// @brief Resets the ReadPort.
  /// @brief 重置ReadPort。
  virtual void Reset();
};

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  enum class LockState : uint32_t
  {
    LOCKED = 0,
    UNLOCKED = UINT32_MAX
  };

  WriteFun write_fun_ = nullptr;
  LockFreeQueue<WriteInfoBlock> *queue_info_;
  LockFreeQueue<uint8_t> *queue_data_;
  std::atomic<LockState> lock_{LockState::UNLOCKED};
  size_t write_size_ = 0;

  /**
   * @brief WritePort 的流式写入操作器，支持链式 << 操作和批量提交。
   * @brief Stream-like writer for WritePort, supporting chainable << operations and batch
   * commit.
   *
   * @endcode
   *
   * 构造时会尝试锁定 WritePort，并批量写入，减少碎片化写操作和队列压力。
   * Automatically acquires the WritePort lock (if possible), enabling batch writes to
   * reduce fragmented write operations and queue pressure.
   */
  class Stream
  {
   public:
    /**
     * @brief 构造流写入对象，并尝试锁定端口。
     * @brief Constructs a Stream object and tries to acquire WritePort lock.
     * @param port 指向 WritePort 的指针 Pointer to WritePort.
     * @param op 写操作对象（可重用）Write operation object (can be reused).
     */
    Stream(LibXR::WritePort *port, LibXR::WriteOperation op);

    /**
     * @brief 析构时自动提交已累积的数据并释放锁。
     * @brief Destructor: automatically commits any accumulated data and releases the
     * lock.
     */
    ~Stream();

    /**
     * @brief 追加写入数据，支持链式调用。
     * @brief Appends data for writing, supporting chain calls.
     * @param data 要写入的数据 Data to write.
     * @return 返回自身引用 Enables chainable call.
     */
    Stream &operator<<(const ConstRawData &data);

    /**
     * @brief 手动提交已写入的数据到队列，并尝试续锁。
     * @brief Manually commit accumulated data to the queue, and try to extend the lock.
     *
     * 调用后已写入数据会立即入队，size 计数归零。适合周期性手动 flush。
     * After calling, written data is enqueued, size counter reset. Suitable for periodic
     * manual flush.
     *
     * @return 返回操作的 `ErrorCode`，指示操作结果。
     *         Returns an `ErrorCode` indicating the result of the operation.
     */
    ErrorCode Commit();

   private:
    LibXR::WritePort *port_;    ///< 写端口指针 Pointer to the WritePort
    LibXR::WriteOperation op_;  ///< 写操作对象 Write operation object
    size_t cap_;                ///< 当前队列容量 Current queue capacity
    size_t size_ = 0;  ///< 当前已写入但未提交的字节数 Bytes written but not yet committed
    bool locked_ = false;                    ///< 是否持有写锁 Whether write lock is held
    bool fallback_to_normal_write_ = false;  ///< 回退为普通写模式（不可批量） Fallback to
                                             ///< normal write (if batch not supported)
  };

  /**
   * @brief 构造一个新的 WritePort 对象。
   *        Constructs a new WritePort object.
   *
   * 该构造函数初始化无锁操作队列 `queue_info_` 和数据块队列 `queue_data_`。
   * This constructor initializes the lock-free operation queue `queue_info_` and the data
   * block queue `queue_data_`.
   *
   * @param queue_size 队列的大小，默认为 3。
   *                   The size of the queue, default is 3.
   * @param block_size 缓存的数据的最大大小，默认为 128。
   *                   The maximum size of cached data, default is 128.
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  WritePort(size_t queue_size = 3, size_t buffer_size = 128);

  /**
   * @brief 获取数据队列的剩余可用空间。
   *        Gets the remaining available space in the data queue.
   *
   * @return 返回数据队列的空闲大小。
   *         Returns the empty size of the data queue.
   */
  virtual size_t EmptySize();

  /**
   * @brief 获取当前数据队列的已使用大小。
   *        Gets the used size of the current data queue.
   *
   * @return 返回数据队列的已使用大小。
   *         Returns the size of the data queue.
   */
  virtual size_t Size();

  /**
   * @brief 判断端口是否可写。
   *        Checks whether the port is writable.
   *
   * @return 如果 `write_fun_` 不为空，则返回 `true`，否则返回 `false`。
   *         Returns `true` if `write_fun_` is not null, otherwise returns `false`.
   */
  bool Writable();

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
  WritePort &operator=(WriteFun fun);

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
  void Finish(bool in_isr, ErrorCode ans, WriteInfoBlock &info, uint32_t size);

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
  void MarkAsRunning(WriteOperation &op);

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
  ErrorCode operator()(ConstRawData data, WriteOperation &op);

  /// @brief Resets the WritePort.
  /// @brief 重置WritePort。
  virtual void Reset();

 private:
  ErrorCode CommitWrite(ConstRawData data, WriteOperation &op, bool pushed = false);
};

/**
 * @brief STDIO interface for read/write ports.
 * @brief 提供静态全局的输入输出接口绑定与缓冲区管理。
 */
class STDIO
{
 public:
  // NOLINTBEGIN
  static inline ReadPort *read_ = nullptr;    ///< Read port instance. 读取端口。
  static inline WritePort *write_ = nullptr;  ///< Write port instance. 写入端口。
  static inline LibXR::Mutex *write_mutex_ =
      nullptr;  ///< Write port mutex. 写入端口互斥锁。
  static inline LibXR::WritePort::Stream *write_stream_ = nullptr;
#if LIBXR_PRINTF_BUFFER_SIZE > 0
  static inline char
      printf_buff_[LIBXR_PRINTF_BUFFER_SIZE];  ///< Print buffer. 打印缓冲区。
#endif
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
  static int Printf(const char *fmt, ...);  // NOLINT
};
}  // namespace LibXR
