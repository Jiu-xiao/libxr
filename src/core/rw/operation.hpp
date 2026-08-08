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
 * @brief Atomic status value for polling operations.
 * @brief 用于轮询操作的原子状态值。
 *
 * The completion context may update this value while another thread or core polls it.
 * The 32-bit storage matches the portable embedded atomic runtime boundary.
 * 完成上下文可以在其他线程或核心轮询时更新该值；32 位存储与嵌入式平台的原子运行时
 * 边界一致。
 */
class OperationPollingStatus
{
 public:
  /** @brief 轮询操作的生命周期状态 / Polling-operation lifecycle state. */
  enum Value : uint32_t
  {
    READY,
    RUNNING,
    DONE,
    ERROR
  };

  /**
   * @brief 构造原子轮询状态 / Construct an atomic polling status
   * @param initial 初始状态 / Initial state
   */
  OperationPollingStatus(Value initial = READY) noexcept
      : value_(static_cast<uint32_t>(initial))
  {
  }

  /**
   * @brief 以 relaxed load 复制当前状态 / Copy the current status with a relaxed load
   * @param other 源状态对象 / Source status object
   */
  OperationPollingStatus(const OperationPollingStatus& other) noexcept
      : value_(static_cast<uint32_t>(other.Load(std::memory_order_relaxed)))
  {
  }

  /**
   * @brief 以 relaxed 顺序复制另一个状态值 / Copy another status with relaxed ordering
   * @param other 源状态对象 / Source status object
   * @return 当前对象 / This object
   */
  OperationPollingStatus& operator=(const OperationPollingStatus& other) noexcept
  {
    Store(other.Load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }

  /**
   * @brief 发布一个新状态 / Publish a new status
   * @param status 新状态值 / New status value
   * @return 当前对象 / This object
   */
  OperationPollingStatus& operator=(Value status) noexcept
  {
    Store(status);
    return *this;
  }

  /**
   * @brief 使用指定 memory order 存储状态 / Store the status with the selected memory
   * order
   * @param status 新状态值 / New status value
   * @param order 原子存储顺序 / Atomic store order
   */
  void Store(Value status, std::memory_order order = std::memory_order_release) noexcept
  {
    value_.store(static_cast<uint32_t>(status), order);
  }

  /**
   * @brief 使用指定 memory order 读取状态 / Load the status with the selected memory
   * order
   * @param order 原子读取顺序 / Atomic load order
   * @return 当前状态值 / Current status value
   */
  Value Load(std::memory_order order = std::memory_order_acquire) const noexcept
  {
    return static_cast<Value>(value_.load(order));
  }

  /** @return 以 acquire 顺序读取的当前状态 / Current status loaded with acquire. */
  operator Value() const noexcept { return Load(); }

 private:
  std::atomic<uint32_t> value_{};
};

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
  using OperationPollingStatus = LibXR::OperationPollingStatus;

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
  {
  }

  /**
   * @brief Constructs a callback-based operation.
   * @brief 构造基于回调的操作。
   * @param callback Callback function reference.
   */
  Operation(Callback& callback)
      : data{.callback = &callback}, type(OperationType::CALLBACK)
  {
  }

  /**
   * @brief Constructs a polling operation.
   * @brief 构造轮询操作。
   * @param status 与完成上下文共享的原子轮询状态 / Atomic polling status shared with
   * the completion context
   */
  Operation(OperationPollingStatus& status)
      : data{.status = &status}, type(OperationType::POLLING)
  {
  }

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
   * @param status 转发给 callback 或 polling 状态的结果 / Result forwarded to the
   * callback or polling status
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
        data.status->Store((status == ErrorCode::OK) ? OperationPollingStatus::DONE
                                                     : OperationPollingStatus::ERROR);
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
      data.status->Store(OperationPollingStatus::RUNNING);
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
 * @brief 同步驱动操作共享的 BLOCK waiter 交接状态 / Shared BLOCK-waiter handoff for
 * synchronous driver operations
 *
 * timeout 会分离等待调用者。迟到的 completion 仍可清理 in-flight 状态，但不再属于已
 * timeout 的调用者。 / Timeout detaches the waiting caller. A late completion may
 * still clear in-flight state, but no longer belongs to that caller.
 */
class AsyncBlockWait
{
 public:
  // Keep the waiter state 32-bit wide so STM32 builds stay within the
  // project-wide atomic shim boundary.
  /** @brief waiter 与 completion 的交接状态 / Waiter-completion handoff state. */
  enum class State : uint32_t
  {
    IDLE = 0,
    PENDING = 1,
    CLAIMED = 2,
    DETACHED = 3,
  };

  /**
   * @brief 使用指定 semaphore 启动一次等待 / Start one wait with a semaphore
   * @param sem 本次等待独占的 semaphore / Semaphore dedicated to this wait
   * @pre 前一次等待已经完成或取消 / Any previous wait has completed or been cancelled
   */
  void Start(Semaphore& sem)
  {
    sem_ = &sem;
    result_ = ErrorCode::OK;
    state_.store(State::PENDING, std::memory_order_release);
  }

  /** @brief 在等待开始前取消当前 handoff / Cancel the current handoff before waiting. */
  void Cancel() { state_.store(State::IDLE, std::memory_order_release); }

  /**
   * @brief 等待 completion 或 timeout / Wait for completion or timeout
   * @param timeout 最大等待毫秒数 / Maximum wait in milliseconds
   * @return completion 结果，或调用者成功分离后的 `TIMEOUT` / Completion result, or
   * `TIMEOUT` after the caller detaches
   */
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

  /**
   * @brief 尝试认领 waiter 并发布 completion / Try to claim the waiter and post
   * completion
   * @param in_isr 是否从 ISR 上下文完成 / Whether completion runs in ISR context
   * @param ec 最终操作结果 / Final operation result
   * @return completion 认领并唤醒 waiter 时为 true；waiter 已分离或操作已空闲时为 false /
   * True when completion claimed and woke the waiter; false after detachment or when the
   * operation is already idle
   */
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
        (void)state_.compare_exchange_strong(
            expected, State::IDLE, std::memory_order_acq_rel, std::memory_order_acquire);
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
