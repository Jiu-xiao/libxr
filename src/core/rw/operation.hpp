#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "semaphore.hpp"

namespace LibXR
{

/**
 * @brief Completion notification descriptor for an operation.
 * @brief 操作的完成通知描述符。
 *
 * @tparam Args The callback argument type.
 * @tparam Args 回调参数类型。
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
   * @param status Reference to polling status.
   */
  Operation(std::atomic<OperationPollingStatus>& status)
      : data{.status = &status}, type(OperationType::POLLING)
  {
  }

  Operation(const Operation&) = default;
  Operation(Operation&&) noexcept = default;

  /**
   * @brief Copy assignment operator.
   * @brief 复制赋值运算符。
   * @return Reference to this operation.
   */
  Operation& operator=(const Operation&) = default;

  /**
   * @brief Move assignment operator.
   * @brief 移动赋值运算符。
   * @return Reference to this operation.
   */
  Operation& operator=(Operation&&) noexcept = default;

  /**
   * @brief Updates operation status based on type.
   * @brief 根据类型更新操作状态。
   * @param in_isr Indicates if executed within an interrupt.
   * @param status Completion status reported by the operation.
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
        data.status->store((status == ErrorCode::OK) ? OperationPollingStatus::DONE
                                                     : OperationPollingStatus::ERROR,
                           std::memory_order_release);
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
      data.status->store(OperationPollingStatus::RUNNING, std::memory_order_release);
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
    std::atomic<OperationPollingStatus>* status;
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

    ErrorCode finish_wait_ans;
    do
    {
      finish_wait_ans = sem_->Wait(UINT32_MAX);
    } while (finish_wait_ans == ErrorCode::TIMEOUT);
    REQUIRE(finish_wait_ans == ErrorCode::OK);
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

// RW metadata is stored in the byte-backed SPSC ring, so its pointer/enum
// operation descriptor must remain safe for representation copying.
// RW 元数据存放在字节型 SPSC 环中，因此其指针/枚举操作描述必须保持可平凡复制。
static_assert(std::is_trivially_copyable_v<ReadOperation>);
static_assert(std::is_trivially_copyable_v<WriteOperation>);
static_assert(std::is_trivially_destructible_v<ReadOperation>);
static_assert(std::is_trivially_destructible_v<WriteOperation>);

/// @brief Function pointer type for write progress notifications.
/// @brief 写入进度通知函数指针类型。
///
/// The request has already reached the WritePort software queue before this callback
/// runs. The backend consumes it through WriteQueue and completes it through scope
/// settlement; the callback has no synchronous result channel.
/// 调用本通知前，请求已经进入 WritePort 软件队列。后端通过 WriteQueue 消费，并由
/// scope 结算完成；本回调不提供同步结果通道。
typedef void (*WriteFun)(WritePort& port, bool in_isr);

}  // namespace LibXR
