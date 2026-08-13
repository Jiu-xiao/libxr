#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  WriteFun write_fun_ =
      nullptr;  ///< Driver/backend write entry. 底层驱动或后端写入入口。
  SPSCQueue<WriteInfoBlock>* queue_info_ =
      nullptr;  ///< Metadata queue for pending write batches. 挂起写批次的元数据队列。
  SPSCQueue<uint8_t>* queue_data_ =
      nullptr;  ///< Payload queue for pending write bytes. 挂起写入字节的数据队列。

 private:
  // Phase serializes the producer and the one deferred direct BLOCK request. ActiveState
  // independently tracks a BLOCK record already published to the backend. HANDOFF keeps
  // admission closed until a deferred waiter consumes its terminal result.
  // Phase 串行化 producer 与唯一一笔 deferred direct BLOCK 请求；ActiveState 独立跟踪
  // 已发布给后端的 BLOCK 记录；HANDOFF 在 deferred waiter 取走最终结果前保持入口关闭。
  enum class Phase : uint32_t
  {
    FREE = 0,
    OWNER = 1,
    WAITING = 2,
    PUBLISHING = 3,
  };

  enum class ActiveState : uint32_t
  {
    NONE = 0,
    WAITING = 1,
    CLAIMED = 2,
    DETACHED = 3,
  };

  static constexpr uint32_t PHASE_MASK = 0x7U;
  static constexpr uint32_t ACTIVE_SHIFT = 3U;
  static constexpr uint32_t ACTIVE_MASK = 0x3U << ACTIVE_SHIFT;
  static constexpr uint32_t KICK = 1U << 5U;
  static constexpr uint32_t PUBLISH_TIMEOUT = 1U << 6U;
  static constexpr uint32_t HANDOFF = 1U << 7U;
  static constexpr uint32_t RESULT_SHIFT = 8U;
  static constexpr uint32_t RESULT_MASK = 0xFFU << RESULT_SHIFT;

  static constexpr uint32_t State(Phase phase, ActiveState active)
  {
    return static_cast<uint32_t>(phase) | (static_cast<uint32_t>(active) << ACTIVE_SHIFT);
  }

  static constexpr Phase CurrentPhase(uint32_t state)
  {
    return static_cast<Phase>(state & PHASE_MASK);
  }

  static constexpr ActiveState Active(uint32_t state)
  {
    return static_cast<ActiveState>((state & ACTIVE_MASK) >> ACTIVE_SHIFT);
  }

  static constexpr uint32_t WithPhase(uint32_t state, Phase phase)
  {
    return (state & ~PHASE_MASK) | static_cast<uint32_t>(phase);
  }

  static constexpr uint32_t WithActive(uint32_t state, ActiveState active)
  {
    return (state & ~ACTIVE_MASK) | (static_cast<uint32_t>(active) << ACTIVE_SHIFT);
  }

  static constexpr uint32_t WithoutPublicationFlags(uint32_t state)
  {
    return state & ~(KICK | PUBLISH_TIMEOUT);
  }

  static constexpr bool HasKick(uint32_t state) { return (state & KICK) != 0U; }

  static constexpr bool HasPublishTimeout(uint32_t state)
  {
    return (state & PUBLISH_TIMEOUT) != 0U;
  }

  static constexpr bool HasHandoff(uint32_t state) { return (state & HANDOFF) != 0U; }

  static constexpr uint32_t WithResult(uint32_t state, ErrorCode result)
  {
    const auto encoded = static_cast<uint32_t>(static_cast<int32_t>(result) + 128);
    return (state & ~RESULT_MASK) | (encoded << RESULT_SHIFT);
  }

  static constexpr ErrorCode Result(uint32_t state)
  {
    const auto encoded = static_cast<int32_t>((state & RESULT_MASK) >> RESULT_SHIFT);
    return static_cast<ErrorCode>(encoded - 128);
  }

  std::atomic<uint32_t> state_{
      WithResult(State(Phase::FREE, ActiveState::NONE), ErrorCode::OK)};
  WriteInfoBlock deferred_info_{};  ///< One direct BLOCK request awaiting admission.
                                    ///< 一笔等待接纳的直接 BLOCK 写请求。

  bool TryAcquireOwner();
  bool TryReserveDeferredOwner();
  void ReleaseOwner(bool in_isr);
  ErrorCode DeferBlock(ConstRawData data, WriteOperation& op, bool owns_port);
  ErrorCode WaitForBlock(WriteOperation& op, bool deferred);
  ErrorCode PublishOwned(ConstRawData data, WriteOperation& op, bool data_pushed,
                         bool in_isr, bool deferred);
  void ReleaseBlockClaim();
  void FinishDeferredPublication(WriteOperation& op, ErrorCode ans, bool in_isr);

 public:
  // Stream batch facade.
  // Stream 负责一次批次的累积写入与提交。
  // One caller owns a Stream between commits. The atomic state rejects callback or
  // asynchronous reentry while a submission is being finalized; it does not make one
  // Stream instance safe for general concurrent use by multiple callers.
  // 两次提交之间由一个调用方独占 Stream。原子状态只负责拒绝提交收尾期间的回调或
  // 异步重入，并不保证多个普通调用方可以并发操作同一个 Stream 实例。
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

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) = delete;
    Stream& operator=(Stream&&) = delete;

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
     * 整个片段原子地追加到当前批次对应的共享 data queue 尾部；Commit() 负责随后发布
     * 这批字节对应的元数据。若空间不足则返回 FULL，并保持该片段完全未写入。
     * This is the low-level semantic write entrypoint of Stream. It acquires the current
     * batch ownership and then attempts to append the whole chunk atomically into the
     * shared data-queue tail; Commit() later publishes the metadata that describes this
     * batch. If space is insufficient it returns FULL and leaves that chunk entirely
     * unwritten.
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
     * 调用后会发布当前批次对应的元数据、使这批已追加到共享 data queue 的字节正式成为
     * 一个可消费的写操作，并将 size 计数归零。适合周期性手动 flush。
     * After calling, the metadata that describes the current batch is published so the
     * bytes already appended into the shared data queue become one consumable write
     * operation, and the size counter is reset. Suitable for periodic manual flush.
     *
     * @return 返回操作的 ErrorCode，指示操作结果。
     *         Returns an ErrorCode indicating the result of the operation.
     */
    ErrorCode Commit();

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
    [[nodiscard]] size_t EmptySize() const;

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

    LibXR::WritePort* port_;    ///< 写端口指针 Pointer to the WritePort
    LibXR::WriteOperation op_;  ///< 写操作对象 Write operation object
    size_t buffered_size_ =
        0;  ///< 当前批次已追加到共享 data queue、但尚未发布对应元数据的字节数 Bytes
            ///< already appended into the shared data queue for the current batch, but
            ///< whose metadata has not yet been published
    enum class StreamState : uint32_t
    {
      RELEASED,
      OWNED,
      SUBMITTING,
    };

    std::atomic<StreamState> state_{StreamState::RELEASED};
  };

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
   * @param info 需要完成的写入记录元数据。
   *             Metadata for the write record being completed.
   * @warning 完成回调可能在无关 producer 持有本端口时运行。回调若重入写同一端口，必须
   *          处理 `BUSY`；完成串行化不会为回调重入保留 producer ownership。 / A
   *          completion callback may run while an unrelated producer owns this port. A
   *          callback that writes the same port must handle `BUSY`; completion
   *          serialization does not reserve producer ownership for callback re-entry.
   */
  void Finish(bool in_isr, ErrorCode ans, WriteInfoBlock& info);

  /**
   * @brief 尝试接纳并发布一笔等待中的 BLOCK 写请求。
   * @brief Tries to admit and publish one deferred BLOCK write request.
   *
   * 后端恢复或消费进度释放了写队列空间后可调用本接口。若空间仍不足、另一个端口路径
   * 正持有提交权，或没有等待请求，本调用直接返回且不会唤醒写入者。
   * Backends may call this after recovery or after consumer progress releases write-queue
   * capacity. The call returns without waking the writer when capacity is still
   * insufficient, another port path owns submission, or no deferred request exists.
   *
   * @param in_isr 当前调用是否位于 ISR。 Whether the call runs in ISR context.
   */
  void ProcessPendingWrites(bool in_isr);

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
   * @warning BLOCK operations are forbidden in ISR context and fail a strong runtime
   *          requirement before any request state is published.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode operator()(ConstRawData data, WriteOperation& op, bool in_isr = false);

  /**
   * @brief 提交写入操作。
   *        Commits a write operation.
   *
   * @param data 写入的原始数据 / Raw data to be written
   * @param op 写入操作对象，包含操作类型和同步机制。
   *           Write operation object containing the operation type and synchronization
   * @param data_pushed 数据是否已经推送到缓冲区 / Whether the data has been pushed to
   * the buffer
   * @param in_isr 指示是否在中断上下文中执行。
   *               Indicates whether the operation is executed in an interrupt context.
   * @return 返回操作的 ErrorCode，指示操作结果。
   *         Returns an ErrorCode indicating the result of the operation.
   */
  ErrorCode CommitWrite(ConstRawData data, WriteOperation& op, bool data_pushed = false,
                        bool in_isr = false);

 protected:
  /**
   * @brief Fail pending work and reset queue state during a quiesced backend teardown.
   * @brief 在后端已静止的 teardown 期间失败挂起操作并重置队列状态。
   *
   * @note This is not a public queue-management API. Call it only after the backend is
   *       known to be unavailable.
   * @note 这不是公开的队列管理接口。仅在后端已明确不可用后调用。
   * @note The surrounding backend must first close new front-end admission, stop
   *       completion and IRQ sources, and wait for every already-admitted submitter or
   *       owner that can mutate this port or its queues to exit.
   * @note 外围后端必须先关闭新的前端请求入口，停止完成与 IRQ 来源，并等待所有已接纳
   *       且可能修改本端口或其队列的 submitter 与 owner 退出。
   * @note A record already popped by a backend must be completed with Finish() or
   *       returned to the queue before this call. Only a BLOCK caller still asleep in
   *       Wait() whose record remains queued may be resolved and woken here. No other
   *       port or queue mutator may begin until this call returns.
   * @note 后端已经弹出的 active record 必须先通过 Finish() 完成或退回队列。只有仍阻塞在
   *       Wait() 且 record 仍在队列中的 BLOCK 调用可以由本函数完成并唤醒；本调用
   *       返回前不得开始其他会修改端口或队列的操作。
   * @note Seeing OWNER here means the caller violated the backend precondition. Debug
   *       builds assert; release builds still return without mutating the queues.
   * @note 若此时仍看到 OWNER，说明调用方违反了后端前提。调试构建会触发断言，发布构建仍会
   *       在不修改队列的情况下返回。
   *
   * @param reason Final failure reported to pending operations. 挂起操作的最终失败原因。
   * @param in_isr Whether the backend teardown runs in ISR context. 后端 teardown
   *               是否运行于 ISR 上下文。
   */
  void FailPendingAndResetForBackendTeardown(ErrorCode reason, bool in_isr);
};

}  // namespace LibXR
