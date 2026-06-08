#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "queue.hpp"
#include "operation.hpp"

namespace LibXR
{

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
  // CLEARING = ClearQueuedData() owns software dequeue progress
  // BLOCK_CLAIMED = wakeup now belongs to the waiter
  // BLOCK_DETACHED = timeout detached the waiter
  // The same semaphore may be reused only after the previous BLOCK call
  // returns and the port goes back to IDLE.
  // 读 BLOCK 状态：
  // PENDING = 已通知 read_fun_，等待队列侧完成
  // CLEARING = ClearQueuedData() 占有软件出队进度
  // BLOCK_CLAIMED = 唤醒已经归当前 waiter 所有
  // BLOCK_DETACHED = timeout 已把 waiter 分离
  // 同一个信号量只能在上一次 BLOCK 调用返回、端口回到 IDLE 后复用。
  enum class BusyState : uint32_t
  {
    IDLE = 0,     ///< No active waiter and no pending completion. 无等待者、无挂起完成。
    PENDING = 1,  ///< Driver accepted the request; completion still owns progress.
                  ///< 请求已交给底层推进。
    CLEARING = 2,  ///< ClearQueuedData() owns software dequeue progress.
                   ///< ClearQueuedData() 占有软件出队进度。
    BLOCK_CLAIMED = 3,   ///< BLOCK wakeup already belongs to the current waiter. 当前
                         ///< BLOCK 唤醒已被本次等待者认领。
    BLOCK_DETACHED = 4,  ///< Timeout detached the waiter; completion must stay silent.
                         ///< 超时已分离等待者，完成侧不得再唤醒。
    EVENT = UINT32_MAX   ///< Data arrived before a waiter was armed; next caller must
                         ///< re-check queue. 数据先到，后续调用者要重查队列。
  };

  ReadFun read_fun_ = nullptr;  ///< Driver/backend read notification entry. 底层驱动或后端读取通知入口。
  SPMCQueue<uint8_t>* queue_data_ = nullptr;  ///< RX payload queue. 接收数据字节队列。
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
   * @brief 清空当前已排队的 RX 字节。
   * @brief Discards the RX bytes currently queued in software.
   *
   * 该接口只丢弃当前 queue_data_ 中已经排队的字节，不参与 backend teardown，也不会
   * 失败完成挂起读请求。若存在正在推进的读请求，则返回 BUSY。
   * This API only discards the bytes already queued in queue_data_. It does not
   * participate in backend teardown and does not fail-complete an in-flight read.
   * Returns BUSY when a read request is currently in progress.
   *
   * @note After this call claims CLEARING, ordinary reads can no longer consume the
   *       current software-queue snapshot. Bytes that arrive after the snapshot may
   *       remain queued for a later reader/clear call.
   * @note 本次调用 claim `CLEARING` 之后，普通读不会再消费当前软件队列快照；在快照
   *       之后新到达的字节，可以留给后续读取或下次清队列。
   *
   * @param in_isr 是否在 ISR 上下文 / Whether running in ISR context
   * @return `OK` 表示本次清队列成功完成；`BUSY` 表示当前有读请求占有该端口。
   *         `OK` means the clear operation completed; `BUSY` means an active read still
   *         owns this port.
   */
  [[nodiscard]] ErrorCode ClearQueuedData(bool in_isr = false);

  /**
   * @brief Processes pending reads.
   * @brief 处理挂起的读取请求。
   *
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   */
  void ProcessPendingReads(bool in_isr);

  /**
   * @brief 失败完成并清空当前所有挂起读操作。
   * @brief Fail-complete and clear all currently pending read operations.
   *
   * @note Driver-only: call this only after the backend is known to be unavailable.
   * @note 仅供驱动层在后端已明确不可用后调用。
   * @note The surrounding driver must already guarantee that no new front-end
   *       submissions or back-end completion/data events can still arrive for
   *       this port.
   * @note 外围驱动还必须先保证：这条端口后续不会再收到新的前端提交，也不会再收到
   *       新的后端完成或数据事件。
   *
   * @param reason 最终失败原因 / Final failure reason
   * @param in_isr 是否在 ISR 上下文 / Whether running in ISR context
   */
  void FailAndClearAll(ErrorCode reason, bool in_isr);
};

}  // namespace LibXR
