#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
#include "print/print_api.hpp"
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
template <typename Args>
class Operation
{
 public:
  using Callback = LibXR::Callback<Args>;

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
    DONE,
    ERROR
  };

  /// @brief Default constructor, initializes with NONE type.
  /// @brief 默认构造函数，初始化为NONE类型。
  Operation() : data{nullptr}, type(OperationType::NONE) {}

  /**
   * @brief Constructs a blocking operation with a semaphore and timeout.
   * @brief 使用信号量和超时构造阻塞操作。
   * @param sem Semaphore reference.
   * @param timeout Timeout duration (default is maximum).
   *
   * @note sem must be dedicated to one live BLOCK call at a time.
   *       Reuse is allowed only after that call returns.
   * @note sem 在任一时刻只能服务一个存活中的 BLOCK 调用；
   *       只有在该调用返回后才能复用。
   */
  Operation(Semaphore& sem, uint32_t timeout = UINT32_MAX)
      : data{.sem_info = {&sem, timeout}}, type(OperationType::BLOCK)
  {}

  /**
   * @brief Constructs a callback-based operation.
   * @brief 构造基于回调的操作。
   * @param callback Callback function reference.
   */
  Operation(Callback& callback)
      : data{.callback = &callback}, type(OperationType::CALLBACK)
  {}

  /**
   * @brief Constructs a polling operation.
   * @brief 构造轮询操作。
   * @param status Reference to polling status.
   */
  Operation(OperationPollingStatus& status)
      : data{.status = &status}, type(OperationType::POLLING)
  {}

  Operation(const Operation& op) : data{nullptr}, type(OperationType::NONE)
  {
    *this = op;
  }

  Operation(Operation&& op) noexcept : data{nullptr}, type(OperationType::NONE)
  {
    *this = std::move(op);
  }

  /**
   * @brief Copy assignment operator.
   * @brief 复制赋值运算符。
   * @param op Another Operation instance.
   * @return Reference to this operation.
   */
  Operation& operator=(const Operation& op)
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
          data.callback = nullptr;
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
  Operation& operator=(Operation&& op) noexcept
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
          data.callback = nullptr;
          break;
      }
    }
    return *this;
  }

  /**
   * @brief Updates operation status based on type.
   * @brief 根据类型更新操作状态。
   * @param in_isr Indicates if executed within an interrupt.
   * @param args Parameters passed to the callback.
   */
  template <typename Status>
  void UpdateStatus(bool in_isr, Status&& status)
  {
    switch (type)
    {
      case OperationType::CALLBACK:
        data.callback->Run(in_isr, std::forward<Status>(status));
        break;
      case OperationType::BLOCK:
        // BLOCK waits are signaled by semaphore only; the owning port keeps the
        // final ErrorCode in its block_result_ handoff state.
        // BLOCK 只通过信号量唤醒；最终 ErrorCode 由端口侧 block_result_ 交接。
        data.sem_info.sem->PostFromCallback(in_isr);
        break;
      case OperationType::POLLING:
        *data.status = (status == ErrorCode::OK) ? OperationPollingStatus::DONE
                                                 : OperationPollingStatus::ERROR;
        break;
      case OperationType::NONE:
        break;
    }
  }

  /**
   * @brief 标记操作为运行状态。
   *        Marks the operation as running.
   *
   * 该函数用于在操作类型为 POLLING 时，将 data.status 设置为 RUNNING，
   * 以指示该操作正在执行中。
   * This function sets data.status to RUNNING when the operation type is POLLING,
   * indicating that the operation is currently in progress.
   *
   * @note 该方法仅适用于 OperationType::POLLING 类型的操作，其他类型不会受到影响。
   *       This method only applies to operations of type OperationType::POLLING,
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
    Callback* callback;
    struct
    {
      Semaphore* sem;
      uint32_t timeout;
    } sem_info;
    OperationPollingStatus* status;
  } data;

  /// Operation type.
  /// 操作类型。
  OperationType type;
};

/**
 * @brief Shared BLOCK waiter handoff for synchronous driver operations.
 *
 * Timeout detaches the waiting caller. A late completion may still clear the
 * in-flight state, but it no longer belongs to that timed-out caller.
 */
class AsyncBlockWait
{
 public:
  // Keep the waiter state 32-bit wide so STM32 builds stay within the
  // project-wide atomic shim boundary.
  enum class State : uint32_t
  {
    IDLE = 0,
    PENDING = 1,
    CLAIMED = 2,
    DETACHED = 3,
  };

  void Start(Semaphore& sem)
  {
    sem_ = &sem;
    result_ = ErrorCode::OK;
    state_.store(State::PENDING, std::memory_order_release);
  }

  void Cancel() { state_.store(State::IDLE, std::memory_order_release); }

  ErrorCode Wait(uint32_t timeout)
  {
    ASSERT(sem_ != nullptr);
    auto wait_ans = sem_->Wait(timeout);
    if (wait_ans == ErrorCode::OK)
    {
#ifdef LIBXR_DEBUG_BUILD
      ASSERT(state_.load(std::memory_order_acquire) == State::CLAIMED);
#endif
      state_.store(State::IDLE, std::memory_order_release);
      return result_;
    }

    State expected = State::PENDING;
    if (state_.compare_exchange_strong(expected, State::DETACHED,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
      return ErrorCode::TIMEOUT;
    }

    ASSERT(expected == State::CLAIMED || expected == State::DETACHED ||
           expected == State::IDLE);
    if (expected == State::DETACHED)
    {
      state_.store(State::IDLE, std::memory_order_release);
      return ErrorCode::TIMEOUT;
    }
    if (expected == State::IDLE)
    {
      return ErrorCode::TIMEOUT;
    }

    auto finish_wait_ans = sem_->Wait(UINT32_MAX);
    UNUSED(finish_wait_ans);
    ASSERT(finish_wait_ans == ErrorCode::OK);
    state_.store(State::IDLE, std::memory_order_release);
    return result_;
  }

  bool TryPost(bool in_isr, ErrorCode ec)
  {
    ASSERT(sem_ != nullptr);

    State expected = State::PENDING;
    if (!state_.compare_exchange_strong(expected, State::CLAIMED,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
    {
      ASSERT(expected == State::DETACHED || expected == State::IDLE);
      if (expected == State::DETACHED)
      {
        expected = State::DETACHED;
        (void)state_.compare_exchange_strong(expected, State::IDLE,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
      }
      return false;
    }

    result_ = ec;
    sem_->PostFromCallback(in_isr);
    return true;
  }

 private:
  Semaphore* sem_ = nullptr;
  std::atomic<State> state_{State::IDLE};
  ErrorCode result_ = ErrorCode::OK;
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
///
/// The current write has already been queued before this function is called. PENDING
/// means the backend owns later completion through Finish(); non-PENDING means the
/// current queued op was consumed/completed synchronously. Negative non-PENDING values
/// report a synchronous start failure and must not leave that op in the queue.
/// 调用该函数前，当前写入已经进入队列。PENDING 表示后端之后通过 Finish() 完成；
/// 非 PENDING 表示当前 queued op 已被同步消费/完成。负数非 PENDING 表示同步启动失败，
/// 且不得把该 op 留在队列中。
typedef ErrorCode (*WriteFun)(WritePort& port, bool in_isr);

/// @brief Function pointer type for read notifications.
/// @brief 读取通知函数指针类型。
///
/// A successful return arms or notifies the backend only. It must not complete the read
/// directly; producers complete reads by pushing bytes into queue_data_ and calling
/// ProcessPendingReads(). Any non-negative return means accepted/armed; negative values
/// mean failure.
/// 成功返回只表示已通知或挂起底层接收，不得直接完成本次读；producer 必须先把字节写入
/// queue_data_，再调用 ProcessPendingReads() 完成读取。返回值非负表示已接受/已挂起，负值表示失败。
typedef ErrorCode (*ReadFun)(ReadPort& port, bool in_isr);

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
  // Exposed low-level state and helpers for the read-path core. Some members stay public
  // because low-level libxr tests and driver glue inspect them directly.
  // 读路径核心的低层状态与辅助类型。部分成员保持 public，
  // 是因为 libxr 的底层测试与驱动胶水层会直接检查它们。

  // Read BLOCK states:
  // PENDING = waiting for queue-fed completion after read_fun_ was notified
  // BLOCK_CLAIMED = wakeup now belongs to the waiter
  // BLOCK_DETACHED = timeout/reset detached the waiter
  // The same semaphore may be reused only after the previous BLOCK call
  // returns and the port goes back to IDLE.
  // 读 BLOCK 状态：
  // PENDING = 已通知 read_fun_，等待队列侧完成
  // BLOCK_CLAIMED = 唤醒已经归当前 waiter 所有
  // BLOCK_DETACHED = timeout/reset 已把 waiter 分离
  // 同一个信号量只能在上一次 BLOCK 调用返回、端口回到 IDLE 后复用。
  enum class BusyState : uint32_t
  {
    IDLE = 0,     ///< No active waiter and no pending completion. 无等待者、无挂起完成。
    PENDING = 1,  ///< Driver accepted the request; completion still owns progress.
                  ///< 请求已交给底层推进。
    BLOCK_CLAIMED = 2,   ///< BLOCK wakeup already belongs to the current waiter. 当前
                         ///< BLOCK 唤醒已被本次等待者认领。
    BLOCK_DETACHED = 3,  ///< Timeout/reset detached the waiter; completion must stay
                         ///< silent. 超时或 Reset 已分离等待者，完成侧不得再唤醒。
    EVENT = UINT32_MAX   ///< Data arrived before a waiter was armed; next caller must
                         ///< re-check queue. 数据先到，后续调用者要重查队列。
  };

  ReadFun read_fun_ = nullptr;  ///< Driver/backend read notification entry. 底层驱动或后端读取通知入口。
  LockFreeQueue<uint8_t>* queue_data_ = nullptr;  ///< RX payload queue. 接收数据字节队列。
  ReadInfoBlock info_{};  ///< In-flight read request metadata. 当前在途读取请求的元数据。
  std::atomic<BusyState> busy_{BusyState::IDLE};  ///< Shared read-progress handoff state. 共享的读进度交接状态。
  ErrorCode block_result_ = ErrorCode::OK;  ///< Final status for the current BLOCK read.

  /**
   * @brief Constructs a ReadPort with queue sizes.
   * @brief 以指定队列大小构造ReadPort。
   * @param buffer_size Size of the RX byte queue.
   *                    接收字节队列的容量。
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  ReadPort(size_t buffer_size = 128);

  /**
   * @brief 获取队列的剩余可用空间。
   *        Gets the remaining available space in the queue.
   *
   * 该函数返回 queue_data_ 中当前可用的空闲空间大小。
   * This function returns the size of the available empty space in queue_data_.
   *
   * @return 返回队列的空闲大小（单位：字节）。
   *         Returns the empty size of the queue (in bytes).
   */
  size_t EmptySize();

  /**
   * @brief 获取当前队列的已使用大小。
   *        Gets the currently used size of the queue.
   *
   * 该函数返回 queue_data_ 当前已占用的空间大小。
   * This function returns the size of the space currently used in queue_data_.
   *
   * @return 返回队列的已使用大小（单位：字节）。
   *         Returns the used size of the queue (in bytes).
   */
  size_t Size();

  /// @brief Checks if read operations are supported.
  /// @brief 检查是否支持读取操作。
  bool Readable();

  /**
   * @brief 赋值运算符重载，用于设置读取函数。
   *        Overloaded assignment operator to set the read function.
   *
   * 该函数允许使用 ReadFun 类型的函数对象赋值给 ReadPort，从而设置 read_fun_。
   * This function allows assigning a ReadFun function object to ReadPort, setting
   * read_fun_.
   *
   * @param fun 要分配的读取函数。
   *            The read function to be assigned.
   * @return 返回自身的引用，以支持链式调用。
   *         Returns a reference to itself for chaining.
   */
  ReadPort& operator=(ReadFun fun);

  /**
   * @brief 完成已由队列路径认领的读取操作。
   *        Completes a read operation already claimed by the queue path.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @param ans 错误码，用于指示操作的结果。
   *            Error code indicating the result of the operation.
   * @param info 需要更新状态的 ReadInfoBlock 引用。
   *             Reference to the ReadInfoBlock whose status needs to be updated.
   */
  void Finish(bool in_isr, ErrorCode ans, ReadInfoBlock& info);

  /**
   * @brief 标记读取操作为运行中。
   *        Marks the read operation as running.
   *
   * 该函数用于将 info.op_ 标记为运行状态，以指示当前正在进行读取操作。
   * This function marks info.op_ as running to indicate an ongoing read operation.
   *
   * @param info 需要更新状态的 ReadInfoBlock 引用。
   *             Reference to the ReadInfoBlock whose status needs to be updated.
   */
  void MarkAsRunning(ReadInfoBlock& info);

  /**
   * @brief 读取操作符重载，用于执行读取操作。
   *        Overloaded function call operator to perform a read operation.
   *
   * 该函数检查端口是否可读，并根据 data.size_ 和 op 的类型执行不同的操作。
   * This function checks if the port is readable and performs different actions based on
   * data.size_ and the type of op.
   *
   * @param data 包含要读取的数据。
   *             Contains the data to be read.
   *
   * @note data.size_ == 0 is a readiness read: it completes when the RX queue is
   *       non-empty, does not consume bytes, and does not call OnRxDequeue().
   * @note data.size_ == 0 表示可读通知：RX 队列非空即完成，不消费字节，也不调用
   *       OnRxDequeue()。
   *
   * @param op 读取操作对象，包含操作类型和同步机制。
   *           Read operation object containing the operation type and synchronization
   * mechanism.
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode operator()(RawData data, ReadOperation& op, bool in_isr = false);

  /**
   * @brief RX 数据从软件队列成功出队后的通知。
   *        Notification after bytes are popped from RX data queue.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   */
  virtual void OnRxDequeue(bool) {}

  /**
   * @brief Processes pending reads.
   * @brief 处理挂起的读取请求。
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   */
  void ProcessPendingReads(bool in_isr);

  /// @brief Resets the ReadPort.
  /// @brief 重置ReadPort。
  void Reset();
};

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  // Exposed low-level state and helpers for the write-path core. Some members stay public
  // because low-level libxr tests and backend glue inspect them directly. Keep the
  // boundary explicit instead of introducing a fake private wall that tests cannot use.
  // 写路径核心的低层状态与辅助类型。部分成员保持 public，
  // 是因为 libxr 的底层测试与后端胶水层会直接检查它们。
  // 这里保持显式边界即可，不做测试本身也用不上的“伪私有化”。

  // Write BLOCK states:
  // LOCKED = submit path owns queue mutation
  // BLOCK_PUBLISHING = BLOCK submit path is publishing queue metadata
  // BLOCK_WAITING = waiter armed, completion not claimed yet
  // BLOCK_CLAIMED = final wakeup belongs to the waiter
  // BLOCK_DETACHED = timeout/reset detached the waiter
  // RESETTING = reset path owns queue mutation
  // The same semaphore may be reused only after the previous BLOCK call
  // returns and the port goes back to IDLE.
  // 写 BLOCK 状态：
  // LOCKED = 提交路径占有队列修改权
  // BLOCK_PUBLISHING = BLOCK 提交路径正在发布队列元数据
  // BLOCK_WAITING = waiter 已挂起，完成尚未 claim
  // BLOCK_CLAIMED = 最终唤醒已经归 waiter 所有
  // BLOCK_DETACHED = timeout/reset 已把 waiter 分离
  // RESETTING = reset 路径占有队列修改权
  // 同一个信号量只能在上一次 BLOCK 调用返回、端口回到 IDLE 后复用。
  enum class BusyState : uint32_t
  {
    LOCKED =
        0,  ///< Submission path owns queue mutation. 提交路径占有写队列/元数据修改权。
    BLOCK_PUBLISHING = 1,  ///< BLOCK submitter is publishing queue metadata.
                           ///< BLOCK 提交者正在发布队列元数据。
    BLOCK_WAITING = 2,  ///< One BLOCK waiter is armed and waiting for final completion.
                        ///< 一个 BLOCK 等待者已经挂起，等待最终完成。
    BLOCK_CLAIMED =
        3,  ///< Final wakeup belongs to the current waiter. 最终唤醒已归当前等待者所有。
    BLOCK_DETACHED = 4,  ///< Waiter already timed out/reset; completion must not post.
                         ///< 等待者已超时/被分离，完成侧不能再 Post。
    RESETTING = 5,        ///< Reset owns queue mutation. Reset 占有写队列/元数据修改权。
    IDLE = UINT32_MAX    ///< No active submitter and no armed BLOCK waiter.
                         ///< 没有活动提交者，也没有挂起中的 BLOCK 等待者。
  };

  WriteFun write_fun_ = nullptr;  ///< Driver/backend write entry. 底层驱动或后端写入入口。
  LockFreeQueue<WriteInfoBlock>* queue_info_ =
      nullptr;  ///< Metadata queue for pending write batches. 挂起写批次的元数据队列。
  LockFreeQueue<uint8_t>* queue_data_ =
      nullptr;  ///< Payload queue for pending write bytes. 挂起写入字节的数据队列。
  std::atomic<BusyState> busy_{BusyState::IDLE};  ///< Shared submit/wait handoff state. 共享的提交/等待交接状态。
  ErrorCode block_result_ = ErrorCode::OK;  ///< Final status for the current BLOCK write. 当前 BLOCK 写入的最终结果。

  // Stream batch facade.
  // Stream 负责一次批次的累积写入与提交。
  class Stream
  {
   public:
    /**
     * @brief 构造流写入对象，并尝试锁定端口。
     * @brief Constructs a Stream object and tries to acquire WritePort lock.
     * @param port 指向 WritePort 的指针 Pointer to WritePort.
     * @param op 写操作对象（可重用）Write operation object (can be reused).
     */
    Stream(LibXR::WritePort* port, LibXR::WriteOperation op);

    /**
     * @brief 析构时自动提交已累积的数据并释放锁。
     * @brief Destructor: automatically commits any accumulated data and releases the
     * lock.
     */
    ~Stream();

    /**
     * @brief 追加一个原始数据片段到当前流批次。
     * @brief Appends one raw-data chunk to the current stream batch.
     *
     * 该接口是 Stream 的底层语义写入入口。它负责拿到当前批次的写入所有权，并尝试将
     * 整个片段原子地追加到当前批次；若空间不足则返回 FULL，并保持该片段完全未写入。
     * This is the low-level semantic write entrypoint of Stream. It acquires the current
     * batch ownership and then attempts to append the whole chunk atomically; if space is
     * insufficient it returns FULL and leaves that chunk entirely unwritten.
     *
     * @param data 要写入的数据片段 Raw-data chunk to append.
     * @return 返回操作结果。Returns the write result.
     */
    [[nodiscard]] ErrorCode Write(ConstRawData data);

    /**
     * @brief 追加一个文本片段到当前流批次。
     * @brief Appends one text chunk to the current stream batch.
     * @param text 要写入的文本片段 Text chunk to append.
     * @return 返回操作结果。Returns the write result.
     */
    [[nodiscard]] ErrorCode Write(std::string_view text)
    {
      return Write(ConstRawData{text.data(), text.size()});
    }

    /**
     * @brief 追加写入数据的语法糖，忽略返回状态并支持链式调用。
     * @brief Syntax sugar for appending data; ignores the status and supports chaining.
     * @param data 要写入的数据 Data to write.
     * @return 返回自身引用 Enables chainable call.
     */
    Stream& operator<<(const ConstRawData& data);

    /**
     * @brief 手动提交已写入的数据到队列，并释放当前锁。
     * @brief Manually commit accumulated data to the queue, then release the current
     * lock.
     *
     * 调用后已写入数据会立即入队，size 计数归零。适合周期性手动 flush。
     * After calling, written data is enqueued, size counter reset. Suitable for periodic
     * manual flush.
     *
     * @return 返回操作的 ErrorCode，指示操作结果。
     *         Returns an ErrorCode indicating the result of the operation.
     */
    ErrorCode Commit();

    /**
     * @brief 丢弃当前批次中尚未提交的内容，并释放当前锁。
     * @brief Discards the current uncommitted batch and releases the current lock.
     */
    void Discard();

    /**
     * @brief 为当前流批次获取一次可写入的端口所有权。
     * @brief Acquires append ownership for the current stream batch.
     * @return 返回获取结果。Returns the acquisition result.
     */
    [[nodiscard]] ErrorCode Acquire();

    /**
     * @brief 获取当前批次还可追加的剩余字节数。
     * @brief Returns the remaining appendable bytes in the current batch.
     *
     * 返回值只在 Acquire 成功后有意义；若当前尚未持有流批次所有权，则返回 0。
     * The return value is meaningful only after Acquire succeeds; it returns zero while
     * this stream does not currently own the batch.
     */
    [[nodiscard]] size_t EmptySize() const
    {
      return owns_port_ ? (batch_capacity_ - buffered_size_) : 0;
    }

   private:
    /**
     * @brief 将当前已缓存批次提交给 WritePort。
     * @brief Submits the currently buffered batch to WritePort.
     *
     * 非 BLOCK 路径下，提交后当前 Stream 仍负责释放端口所有权；BLOCK 路径下，
     * 所有权会交给 WritePort 的等待状态机继续管理。
     * On non-BLOCK paths, this Stream still releases the port ownership after submission;
     * on BLOCK paths, ownership is handed off to WritePort's wait-state machine.
     */
    [[nodiscard]] ErrorCode SubmitBuffered();

    /**
     * @brief 将当前批次的端口所有权归还给 WritePort。
     * @brief Releases the current batch ownership back to WritePort.
     */
    void Release();

    LibXR::WritePort* port_;  ///< 写端口指针 Pointer to the WritePort
    LibXR::WriteOperation op_;  ///< 写操作对象 Write operation object
    size_t batch_capacity_ = 0;  ///< 当前批次可用的总容量 Total capacity reserved for the current batch
    size_t buffered_size_ = 0;  ///< 当前已写入但未提交的字节数 Bytes buffered but not yet committed
    bool owns_port_ = false;  ///< 当前 Stream 是否持有该批次的端口所有权 Whether this Stream currently owns the batch
  };

  // Direct WritePort operations and state-machine entrypoints.
  // WritePort 本体的直接写接口与状态机入口。
  /**
   * @brief 构造一个新的 WritePort 对象。
   *        Constructs a new WritePort object.
   *
   * 该构造函数初始化无锁操作队列 queue_info_ 和数据块队列 queue_data_。
   * This constructor initializes the lock-free operation queue queue_info_ and the data
   * block queue queue_data_.
   *
   * @param queue_size 队列的大小，默认为 3。
   *                   The size of the queue, default is 3.
   * @param buffer_size 缓存数据字节队列的容量，默认为 128。
   *                    Capacity of the cached-byte queue, default is 128.
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
  size_t EmptySize();

  /**
   * @brief 获取当前数据队列的已使用大小。
   *        Gets the used size of the current data queue.
   *
   * @return 返回数据队列的已使用大小。
   *         Returns the size of the data queue.
   */
  size_t Size();

  /**
   * @brief 判断端口是否可写。
   *        Checks whether the port is writable.
   *
   * @return 如果 write_fun_ 不为空，则返回 true，否则返回 false。
   *         Returns true if write_fun_ is not null, otherwise returns false.
   */
  bool Writable();

  /**
   * @brief 赋值运算符重载，用于设置写入函数。
   *        Overloaded assignment operator to set the write function.
   *
   * 该函数允许使用 WriteFun 类型的函数对象赋值给 WritePort，从而设置 write_fun_。
   * This function allows assigning a WriteFun function object to WritePort, setting
   * write_fun_.
   *
   * @param fun 要分配的写入函数。
   *            The write function to be assigned.
   * @return 返回自身的引用，以支持链式调用。
   *         Returns a reference to itself for chaining.
   */
  WritePort& operator=(WriteFun fun);

  /**
   * @brief 更新写入操作的状态。
   *        Updates the status of the write operation.
   *
   * 该函数在写入操作完成时更新对应 info 的状态，并调用 UpdateStatus 更新 op。
   * This function updates the status stored in info and calls UpdateStatus on op when a
   * write operation completes.
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @param ans 错误码，用于指示操作的结果。
   *            Error code indicating the result of the operation.
   * @param op 需要更新状态的 WriteOperation 引用。
   *           Reference to the WriteOperation whose status needs to be updated.
   */
  void Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info);

  /**
   * @brief 标记写入操作为运行中。
   *        Marks the write operation as running.
   *
   * 该函数用于将 op 标记为运行状态，以指示当前正在进行写入操作。
   * This function marks op as running to indicate an ongoing write operation.
   *
   * @param op 需要更新状态的 WriteOperation 引用。
   *           Reference to the WriteOperation whose status needs to be updated.
   */
  void MarkAsRunning(WriteOperation& op);

  /**
   * @brief 执行写入操作。
   *        Performs a write operation.
   *
   * 该函数检查端口是否可写，并根据 data.size_ 和 op 的类型执行不同的操作。
   * This function checks if the port is writable and performs different actions based on
   * data.size_ and the type of op.
   *
   * @param data 需要写入的原始数据。
   *             Raw data to be written.
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * mechanism.
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode operator()(ConstRawData data, WriteOperation& op, bool in_isr = false);

  /// @brief Resets queued write state when no submitter owns queue mutation.
  /// @brief 当没有提交者占有队列修改权时，重置写队列状态。
  void Reset();

  /**
   * @brief 提交写入操作。
   *        Commits a write operation.
   *
   * @param data 写入的原始数据 / Raw data to be written
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * @param pushed 数据是否已经推送到缓冲区 / Whether the data has been pushed to the
   * buffer
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode CommitWrite(ConstRawData data, WriteOperation& op, bool pushed = false,
                        bool in_isr = false);
};

/**
 * @brief STDIO interface for read/write ports.
 * @brief 提供静态全局的输入输出接口绑定与写会话管理。
 */
class STDIO
{
 public:
  // Shared global stdio binding state.
  // 共享的全局 stdio 绑定状态。
  // NOLINTBEGIN
  static inline ReadPort* read_ = nullptr;    ///< Read port instance. 读取端口。
  static inline WritePort* write_ = nullptr;  ///< Write port instance. 写入端口。
  static inline LibXR::Mutex* write_mutex_ =
      nullptr;  ///< Write port mutex. 写入端口互斥锁。
  static inline LibXR::WritePort::Stream* write_stream_ =
      nullptr;  ///< Optional externally owned write stream. 可选的外部托管写流。
  // NOLINTEND

 private:
  // Compiled-format bridge shared by the brace and printf frontends.
  // brace 与 printf 两个前端共用的编译格式桥接层。

  /**
   * @brief STDIO 编译格式会话使用的流式截断输出端。
   * @brief Stream-backed truncating sink used by one STDIO compiled-format session.
   */
  class CompiledSink
  {
   public:
    /**
     * @brief 构造一个绑定到指定流的编译格式输出端。
     * @brief Constructs one compiled-format sink bound to the given stream.
     */
    explicit CompiledSink(WritePort::Stream& stream);

    /**
     * @brief 追加一个文本片段；必要时按会话剩余空间直接截断。
     * @brief Appends one text chunk and truncates directly to the remaining session
     * capacity when needed.
     */
    [[nodiscard]] ErrorCode Write(std::string_view chunk);

    /**
     * @brief 返回当前会话最终保留下来的字节数。
     * @brief Returns the retained byte count of the current session.
     */
    [[nodiscard]] size_t RetainedSize() const { return retained_size_; }

   private:
    WritePort::Stream& stream_;  ///< Active stream session receiving retained bytes. 接收保留字节的活动流会话。
    size_t retained_size_ = 0;  ///< Bytes retained so far. 当前已保留的字节数。
    bool saturated_ = false;  ///< No more bytes should be retained in this session. 当前会话不再继续保留输出。
  };

  /// @brief Type-erased bridge for one compiled STDIO call. / 一次编译格式 STDIO 调用的类型擦除桥接函数。
  using CompiledWriteFun = ErrorCode (*)(void* context, CompiledSink& sink);

  /**
   * @brief 一次编译格式 STDIO 调用的模板上下文。
   * @brief Template context for one compiled-format STDIO call.
   */
  template <typename CompiledFormat, typename... Args>
  struct CompiledCall
  {
    const CompiledFormat& format;  ///< Compile-time compiled format object. 编译期已编译的格式对象。
    std::tuple<Args&&...> args;  ///< Forwarded runtime arguments. 转发保存的运行时参数。

    /**
     * @brief 将当前模板上下文桥接到编译格式前端写入入口。
     * @brief Bridges the current template context into the compiled-format write entry.
     */
    [[nodiscard]] static ErrorCode Write(void* context, CompiledSink& sink)
    {
      auto& compiled_call = *static_cast<CompiledCall*>(context);
      return std::apply(
          [&](auto&&... unpacked) -> ErrorCode
          {
            return Print::Write(sink, compiled_call.format,
                                std::forward<decltype(unpacked)>(unpacked)...);
          },
          compiled_call.args);
    }
  };

  /**
   * @brief 在指定 Stream 上执行一次完整的 STDIO 编译格式写入与收尾。
   * @brief Runs one complete STDIO compiled-format write/finalize pass on the given
   * Stream.
   *
   * 该 helper 统一负责：构造 CompiledSink、调用前端桥接函数、再用
   * FinishWriteSession() 做最终提交或丢弃。
   * This helper centralizes: constructing CompiledSink, invoking the frontend bridge,
   * then finalizing through FinishWriteSession().
   */
  [[nodiscard]] static int WriteCompiledToStream(WritePort::Stream& stream,
                                                 void* context,
                                                 CompiledWriteFun write_fun);

  /**
   * @brief 执行一次完整的 STDIO 编译格式流会话选择、写入与收尾。
   * @brief Runs one complete STDIO compiled-format stream session: stream selection,
   * write, and finalization.
   *
   * 若当前已存在外部绑定的 write_stream_，则复用它；否则创建一个临时的
   * WritePort::Stream 供本次会话使用。
   * Reuses the externally bound write_stream_ when available; otherwise creates one
   * temporary WritePort::Stream for the current session.
   */
  [[nodiscard]] static int WriteCompiledSession(void* context, CompiledWriteFun write_fun);

  /**
   * @brief 执行一次模板已知的 STDIO 编译格式会话。
   * @brief Runs one STDIO compiled-format session whose format/argument types are already
   * known at compile time.
   *
   * 该 helper 只保留模板相关的最薄一层：拿共享会话，再把类型化调用对象交给
   * WriteCompiledSession()。
   * This helper keeps only the thinnest template layer: acquire the shared session, then
   * pass the typed call object into WriteCompiledSession().
   */
  template <typename Call>
  [[nodiscard]] static int RunCompiledSession(Call& call)
  {
    if (!BeginWriteSession())
    {
      return -1;
    }

    return WriteCompiledSession(&call, &Call::Write);
  }

  /**
   * @brief 用一份已编译格式和一组运行时参数执行一次完整的 STDIO 写会话。
   * @brief Runs one complete STDIO write session with one compiled format and one
   * runtime argument pack.
   */
  template <typename CompiledFormat, typename... Args>
  [[nodiscard]] static int RunCompiled(const CompiledFormat& format, Args&&... args)
  {
    CompiledCall<CompiledFormat, Args...> call{
        format, std::forward_as_tuple(std::forward<Args>(args)...)};
    return RunCompiledSession(call);
  }

  /**
   * @brief 获取一个共享的 STDIO 写入会话。
   * @brief Acquires one shared STDIO write session.
   */
  [[nodiscard]] static bool BeginWriteSession();

  /**
   * @brief 提交当前编译格式会话的写入流并释放共享会话。
   * @brief Commits the current compiled-format session stream and releases the shared
   * session.
   */
  [[nodiscard]] static int FinishWriteSession(WritePort::Stream& stream,
                                              size_t retained_size,
                                              ErrorCode format_result);

 public:

  /**
   * @brief Prints one compile-time brace literal to the active STDIO output.
   * @brief 将一个编译期 brace 字面量打印到当前 STDIO 输出。
   * @return Returns the byte count actually retained and committed to the
   *         current STDIO stream; returns -1 on session or commit failure.
   *         返回当前 STDIO 流实际保留并提交的字节数；若会话建立或提交失败，
   *         则返回 -1。
   */
  template <Print::Text Source, typename... Args>
  static int Print(Args&&... args)
  {
    constexpr LibXR::Format<Source> format{};
    return RunCompiled(format, std::forward<Args>(args)...);
  }

  /**
   * @brief Prints one compile-time printf literal to the active STDIO output.
   * @brief 将一个编译期 printf 字面量打印到当前 STDIO 输出。
   * @return Returns the byte count actually retained and committed to the
   *         current STDIO stream; returns -1 on session or commit failure.
   *         返回当前 STDIO 流实际保留并提交的字节数；若会话建立或提交失败，
   *         则返回 -1。
   */
  template <Print::Text Source, typename... Args>
  static int Printf(Args&&... args)
  {
    constexpr auto format = Print::Printf::Build<Source>();
    return RunCompiled(format, std::forward<Args>(args)...);
  }
};
}  // namespace LibXR
