#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

class Pipe;

/**
 * @brief WritePort class for handling write operations.
 * @brief 处理写入操作的WritePort类。
 */
class WritePort
{
 public:
  WriteFun write_fun_ =
      nullptr;  ///< Driver/backend write entry. 底层驱动或后端写入入口。

  /** @brief Read-only pending metadata queue view. / 挂起元数据队列的只读视图。 */
  const SPSCQueue<WriteInfoBlock>* QueueInfo() const noexcept { return queue_info_; }

  /** @brief Read-only pending payload queue view. / 挂起负载队列的只读视图。 */
  const SPSCQueue<uint8_t>* QueueData() const noexcept { return queue_data_; }

  /**
   * @brief Try to publish an asynchronous backend completion.
   *
   * A consumer may claim and transfer data while the producer is publishing metadata.
   * Before asynchronous `Finish()`, or another terminal action based on the same
   * empty-queue observation such as packet flush/ZLP, call this method. `PUBLISHING`
   * records one coalesced retry and returns false; the backend retains the terminal and
   * retries after the admission releaser re-invokes `WriteFun`. `WAITING` and `OWNER`
   * have not made the current record visible, so a terminal observed there belongs to an
   * older record and is allowed.
   * Synchronous completion returned through the current WriteFun stack does not call this
   * method. Backends without a terminal slot, such as the common UART DMA model, call it
   * immediately before the hardware action that defines operation completion. A true
   * result is a point-in-time publication permission, not a lease across the later
   * `Finish()` and callback; the caller must already have fixed which terminal it owns. /
   * consumer
   * 可在 producer 发布 metadata 期间接管和搬运数据。异步调用 `Finish()`，或执行基于同一次
   * 空队列观察的其他 terminal 动作（例如 packet flush/ZLP）前，必须调用本方法；
   * `PUBLISHING` 会合并一次 retry 并返回 false，后端保留 terminal，等待 admission
   * releaser 重新调用 `WriteFun` 后重试。`WAITING` 和 `OWNER` 尚未让当前记录可见，因此
   * 此时观察到的 terminal 属于更早记录，可以直接完成。当前 WriteFun
   * 栈返回的同步完成不调用 本方法。没有 terminal 槽的后端（例如公共 UART DMA
   * model）在定义 operation completion 的硬件动作 前调用。返回 true
   * 只是当前时刻的发布许可，不会跨后续 `Finish()` 和 callback 持有
   * admission；调用方在调用前必须已经确定自己持有的 terminal。
   *
   * @return True when completion may be published now. / 当前可以发布 completion 时为
   * true。
   * @pre A backend that returns false must retain its terminal and return `PENDING` when
   *      `WriteFun` is re-invoked. / 返回 false 的后端必须保留 terminal，并在 `WriteFun`
   *      被重新调用时返回 `PENDING`。
   */
  [[nodiscard]] bool TryPublishBackendCompletion() noexcept;

  /**
   * @brief Scoped backend dequeue transaction. / 后端出队事务作用域。
   *
   * Successful dequeues release queue capacity, but deferred publication is postponed
   * until this scope is destroyed. The backend must publish any local active/pending
   * state before leaving the scope so synchronous write notification cannot observe a
   * half-owned record. / 成功出队会释放队列容量，但 deferred 发布延迟到本作用域析构；
   * 后端必须在离开作用域前发布本地 active/pending 状态，避免同步写通知看到只接管一半的
   * 记录。
   */
  class DequeueScope
  {
   public:
    DequeueScope(const DequeueScope&) = delete;
    DequeueScope& operator=(const DequeueScope&) = delete;
    DequeueScope(DequeueScope&&) = delete;
    DequeueScope& operator=(DequeueScope&&) = delete;

    /** @brief Publish accumulated queue-space progress. / 发布累计的队列空间进展。 */
    ~DequeueScope() noexcept;

    /**
     * @brief Dequeue one write metadata record. / 出队一条写元数据记录。
     * @param info Receives the dequeued record. / 接收出队记录。
     * @return Queue result. / 队列操作结果。
     */
    ErrorCode PopInfo(WriteInfoBlock& info);

    /**
     * @brief Dequeue write payload bytes. / 出队写负载字节。
     * @param data Destination buffer; null discards the bytes. / 目标缓冲；为空时丢弃。
     * @param size Number of bytes to dequeue. / 出队字节数。
     * @return Queue result. / 队列操作结果。
     */
    ErrorCode PopData(uint8_t* data, size_t size);

    /**
     * @brief Discard write payload bytes. / 丢弃写负载字节。
     * @param size Number of bytes to discard. / 丢弃字节数。
     * @return Queue result. / 队列操作结果。
     */
    ErrorCode DiscardData(size_t size) { return PopData(nullptr, size); }

    /**
     * @brief Dequeue payload through a reader callback. / 通过读取回调出队负载。
     * @tparam Reader Reader callback type. / 读取回调类型。
     * @param size Number of bytes to dequeue. / 出队字节数。
     * @param reader Callback receiving contiguous queue spans. / 接收连续队列片段的回调。
     * @return Queue or callback result. / 队列或回调结果。
     */
    template <typename Reader>
    ErrorCode PopDataWithReader(size_t size, Reader&& reader)
    {
      ASSERT(port_.queue_data_ != nullptr);
      const ErrorCode result =
          port_.MutableDataQueue().PopWithReader(size, std::forward<Reader>(reader));
      RecordProgress(result, size);
      return result;
    }

   private:
    friend class WritePort;

    explicit DequeueScope(WritePort& port, bool in_isr) : port_(port), in_isr_(in_isr) {}

    void RecordProgress(ErrorCode result, size_t released)
    {
      progressed_ = progressed_ || (result == ErrorCode::OK && released != 0U);
    }

    void RecordSharedDataDequeue() { progressed_ = true; }

    WritePort& port_;
    bool in_isr_;
    bool progressed_ = false;
  };

 private:
  friend class Pipe;

  SPSCQueue<WriteInfoBlock>* const queue_info_ =
      nullptr;  ///< Metadata queue for pending write batches. 挂起写批次的元数据队列。
  SPSCQueue<uint8_t>* const queue_data_ =
      nullptr;  ///< Payload queue for pending write bytes. 挂起写入字节的数据队列。

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
  static constexpr uint32_t DEFERRED_WAIT_ERROR = 1U << 6U;
  static constexpr uint32_t HANDOFF = 1U << 7U;
  static constexpr uint32_t RESULT_SHIFT = 8U;
  static constexpr uint32_t RESULT_MASK = 0xFFU << RESULT_SHIFT;
  static constexpr uint32_t BACKEND_RETRY = 1U << 16U;

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

  static constexpr uint32_t WithoutTransientFlags(uint32_t state)
  {
    return state & ~(KICK | DEFERRED_WAIT_ERROR | BACKEND_RETRY);
  }

  static constexpr bool HasKick(uint32_t state) { return (state & KICK) != 0U; }

  static constexpr bool HasDeferredWaitError(uint32_t state)
  {
    return (state & DEFERRED_WAIT_ERROR) != 0U;
  }

  static constexpr bool HasHandoff(uint32_t state) { return (state & HANDOFF) != 0U; }

  static constexpr bool HasBackendRetry(uint32_t state)
  {
    return (state & BACKEND_RETRY) != 0U;
  }

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
  SPSCQueue<WriteInfoBlock>& MutableInfoQueue() { return *queue_info_; }
  SPSCQueue<uint8_t>& MutableDataQueue() { return *queue_data_; }
  bool ReleaseOwner(bool in_isr);
  void BeginPublication(bool in_isr);
  void NotifyBackendRetry(bool retry_requested, bool in_isr);
  ErrorCode DeferBlock(ConstRawData data, WriteOperation& op, bool owns_port);
  ErrorCode WaitForBlock(WriteOperation& op, bool deferred);
  ErrorCode PublishOwned(ConstRawData data, WriteOperation& op, bool data_pushed,
                         bool in_isr, bool deferred);
  void ReleaseBlockClaim();
  void FinishDeferredPublication(WriteOperation& op, ErrorCode ans, bool in_isr);
  void ProcessPendingWrites(bool in_isr);
  void RecordSharedDataDequeue(bool in_isr);

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
   * @brief Begin one backend dequeue transaction. / 开始一次后端出队事务。
   * @param in_isr Whether the transaction runs in ISR context. / 是否在 ISR 上下文。
   * @return Scoped dequeue transaction. / 出队事务作用域。
   */
  [[nodiscard]] DequeueScope BeginDequeue(bool in_isr)
  {
    return DequeueScope(*this, in_isr);
  }

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
};

}  // namespace LibXR
