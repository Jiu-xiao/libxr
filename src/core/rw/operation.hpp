#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
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

}  // namespace LibXR
