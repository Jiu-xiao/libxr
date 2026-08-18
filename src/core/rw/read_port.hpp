#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

/**
 * @brief ReadPort class for handling read operations.
 * @brief 处理读取操作的ReadPort类。
 */
class ReadPort
{
 public:
  /**
   * @brief Short-lived producer access to the RX payload queue.
   * @brief RX payload 队列的短生命周期 producer 访问对象。
   *
   * Successful positive-length pushes require an explicit Publish() to notify pending
   * reads. Publish() is terminal and may synchronously invoke OnRxDequeue() and user
   * completion callbacks. Destruction never notifies readers; a dirty unpublished scope
   * is diagnosed in debug builds.
   * 正长度 push 成功后，producer 必须显式调用 Publish() 通知挂起读请求。Publish() 是终止
   * 操作，可能同步调用 OnRxDequeue() 和用户完成回调。析构不会通知 reader；debug 构建会
   * 诊断已写入但未发布的 scope。
   * @warning Producer scopes for one port must be externally serialized.
   *          同一端口的 producer scope 必须由调用方串行化。
   */
  class ReadQueue
  {
   public:
    ReadQueue(const ReadQueue&) = delete;
    ReadQueue& operator=(const ReadQueue&) = delete;
    ReadQueue(ReadQueue&&) = delete;
    ReadQueue& operator=(ReadQueue&&) = delete;

    ~ReadQueue();

    /**
     * @brief Push one byte into the RX payload queue.
     * @brief 向 RX payload 队列写入一个字节。
     */
    [[nodiscard]] ErrorCode Push(uint8_t data)
    {
      if (!CanPush())
      {
        return ErrorCode::STATE_ERR;
      }

      const ErrorCode result = port_->queue_data_->Push(data);
      RecordPushResult(1U, result);
      return result;
    }

    /**
     * @brief Push a complete byte batch into the RX payload queue.
     * @brief 向 RX payload 队列完整写入一批字节。
     */
    [[nodiscard]] ErrorCode PushBatch(const uint8_t* data, size_t size);

    /**
     * @brief Fill and atomically commit a byte batch through a writer callback.
     * @brief 通过 writer 回调填充并原子提交一批字节。
     *
     * The writer may be called once per contiguous queue span. The batch is published to
     * the queue only when every writer call succeeds.
     * writer 可能对每段连续队列空间调用一次；仅当所有调用都成功时才提交整批数据。
     */
    template <typename Writer>
    [[nodiscard]] ErrorCode PushWithWriter(size_t size, Writer&& writer)
    {
      if (!CanPush())
      {
        return ErrorCode::STATE_ERR;
      }

      const ErrorCode result =
          port_->queue_data_->PushWithWriter(size, std::forward<Writer>(writer));
      RecordPushResult(size, result);
      return result;
    }

    /**
     * @brief Return the producer space currently available in the RX payload queue.
     * @brief 返回 RX payload 队列当前可供 producer 使用的空间。
     */
    [[nodiscard]] size_t EmptySize() const;

    /**
     * @brief Return the RX payload queue capacity.
     * @brief 返回 RX payload 队列容量。
     */
    [[nodiscard]] size_t Capacity() const;

    /**
     * @brief Publish successful pushes and terminate this producer scope.
     * @brief 发布成功写入的数据并终止当前 producer scope。
     *
     * A clean scope is a no-op. The scope is marked terminal before any read completion
     * or user callback can run. Afterward, pushes return ErrorCode::STATE_ERR and another
     * Publish() is a no-op in non-maintainer builds; maintainer assertions diagnose both.
     * 未写入数据的 scope 不执行推进；在任何读完成或用户回调运行前，scope 已先标记为终止。
     * 此后，普通构建中的 push 返回 ErrorCode::STATE_ERR，重复 Publish() 不执行操作；
     * maintainer assert 构建会诊断这两种误用。
     */
    void Publish();

   private:
    friend class ReadPort;

    ReadQueue(ReadPort& port, bool in_isr) : port_(&port), in_isr_(in_isr) {}

    [[nodiscard]] bool CanPush() const
    {
      DEV_ASSERT_FROM_CALLBACK(!finished_, in_isr_);
      return !finished_;
    }

    void RecordPushResult(size_t size, ErrorCode result)
    {
      if (size > 0U && result == ErrorCode::OK)
      {
        dirty_ = true;
      }
    }

    ReadPort* const port_;
    const bool in_isr_;
    bool dirty_ = false;
    bool finished_ = false;
  };

 private:
  // Read states:
  // PENDING = one published request is waiting for queue-fed completion
  // CLAIMED = request publication or one completion/clear path owns queue progress
  // CLAIMED_WITH_WAITER = completion owns progress and a failed BLOCK wait is registered
  // BLOCK_CLAIMED = terminal BLOCK wakeup belongs to the waiter
  // The same semaphore may be reused only after the previous BLOCK call
  // returns and the port goes back to IDLE.
  // 读 BLOCK 状态：
  // PENDING = 一个已发布请求正在等待队列侧完成
  // CLAIMED = 请求发布或一个完成/清队列路径占有队列进度
  // CLAIMED_WITH_WAITER = completion 占有进度，且 BLOCK 等待错误已登记等待交接
  // BLOCK_CLAIMED = BLOCK 最终唤醒已经归当前 waiter 所有
  // 同一个信号量只能在上一次 BLOCK 调用返回、端口回到 IDLE 后复用。
  enum class BusyState : uint32_t
  {
    IDLE = 0,     ///< No active waiter and no pending completion. 无等待者、无挂起完成。
    PENDING = 1,  ///< One request is published and awaits queue-side completion.
                  ///< 一个请求已发布，正在等待队列侧完成。
    CLAIMED = 2,  ///< Request publication or queue progress is currently owned.
                  ///< 请求发布或队列进度当前已被占有。
    CLAIMED_WITH_WAITER = 3,  ///< A failed BLOCK wait awaits the current owner handoff.
                              ///< BLOCK 等待错误正在等待当前 owner 交接。
    BLOCK_CLAIMED = 4,        ///< The terminal BLOCK wakeup belongs to the waiter.
                              ///< BLOCK 最终唤醒已归等待者所有。
    // EVENT is a producer-progress carrier bit, not another logical owner. A producer
    // sets it after publishing data when it cannot directly claim an exact PENDING state;
    // the same RMW also carries queue visibility through idle/publication races.
    // EVENT 是 producer 进度载体位，不是另一种逻辑 owner。producer 发布数据后，若无法
    // 直接 claim 精确的 PENDING 状态就设置该位；同一 RMW 也为 idle/publication 竞争携带
    // 队列可见性。
    EVENT = 1U << 31U,
  };

  struct StateMachine;

  struct Request
  {
    RawData data;
    ReadOperation op;
  };

  SPSCQueue<uint8_t>* queue_data_ = nullptr;  ///< Private RX payload queue.
                                              ///< 私有 RX payload 队列。
  Request info_{};  ///< In-flight read request metadata. 当前在途读取请求的元数据。
  std::atomic<BusyState> busy_{
      BusyState::IDLE};  ///< Shared read-progress handoff state. 共享的读进度交接状态。
  ErrorCode block_result_ = ErrorCode::OK;  ///< Final status for the current BLOCK read.

  bool TryCompleteClaimedRead(bool in_isr, bool signal_block_completion);
  void PublishProduced(bool in_isr);

 public:
  /**
   * @brief Constructs a ReadPort with queue sizes.
   * @brief 以指定队列大小构造ReadPort。
   * @param buffer_size Size of the RX byte queue.
   *                    接收字节队列的容量。
   *
   * @note `buffer_size == 0` disables RX: `Readable()` returns false and read requests
   *       return `ErrorCode::NOT_SUPPORT`.
   * @note `buffer_size == 0` 表示禁用 RX：`Readable()` 返回 false，读请求返回
   *       `ErrorCode::NOT_SUPPORT`。
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  ReadPort(size_t buffer_size = 128);

  /**
   * @brief 虚析构函数，保证通过基类指针析构派生读端口的安全性。
   *        Virtual destructor for safe destruction of derived read ports through a base
   *        pointer.
   *
   * @note 仅补齐虚析构以消除“有虚函数却非虚析构”的编译告警；不改变析构行为，
   *       裸指针成员的所有权语义与此前保持一致。
   *       Only adds the virtual destructor to silence the non-virtual-dtor warning; the
   *       destruction behavior is unchanged and raw-pointer member ownership stays as
   *       before.
   */
  virtual ~ReadPort() = default;

  /**
   * @brief Begin one short-lived RX producer scope.
   * @brief 开始一个短生命周期 RX producer scope。
   * @param in_isr Whether publishing this scope runs in ISR context.
   *               当前 scope 是否在 ISR 上下文中发布。
   * @return A non-copyable producer object whose Publish() boundary is explicit.
   *         一个不可复制、显式以 Publish() 终止的 producer 对象。
   * @pre Readable() must be true. / Readable() 必须为 true。
   */
  [[nodiscard]] ReadQueue GetReadQueue(bool in_isr = false);

  /**
   * @brief 获取队列的剩余可用空间。
   *        Gets the remaining available space in the queue.
   *
   * 该函数返回软件 RX 队列中当前可用的空闲空间大小。
   * This function returns the space currently available in the software RX queue.
   *
   * @return 返回队列的空闲大小（单位：字节）。
   *         Returns the empty size of the queue (in bytes).
   */
  size_t EmptySize();

  /**
   * @brief 获取当前队列的已使用大小。
   *        Gets the currently used size of the queue.
   *
   * 该函数返回软件 RX 队列当前已占用的空间大小。
   * This function returns the space currently used in the software RX queue.
   *
   * @return 返回队列的已使用大小（单位：字节）。
   *         Returns the used size of the queue (in bytes).
   */
  size_t Size();

  /**
   * @brief Return the configured RX payload queue capacity.
   * @brief 返回配置的 RX payload 队列容量。
   * @return Zero when RX is disabled, otherwise the byte capacity.
   *         RX 禁用时返回零，否则返回字节容量。
   */
  [[nodiscard]] size_t Capacity() const;

  /**
   * @brief Checks whether this endpoint has a queue-backed read path.
   * @brief 检查该端点是否具有队列驱动的读取路径。
   *
   * This is a queue capability query only. It does not arm a backend and does not
   * imply support for a synchronous pull implementation.
   * 本函数只查询队列能力，不会启动后端，也不表示支持同步 pull 实现。
   */
  bool Readable();

  /**
   * @brief 读取操作符重载，用于执行读取操作。
   *        Overloaded function call operator to perform a read operation.
   *
   * 请求发布后仅由软件 RX 队列完成；producer 必须通过 ReadQueue 入队，
   * 再调用 ReadQueue::Publish()。Requests are completed only from the software
   * RX queue; producers must enqueue through ReadQueue and then call
   * ReadQueue::Publish().
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
   * @warning BLOCK operations are forbidden in ISR context and fail a strong runtime
   *          requirement before any request state is published.
   * @warning A completion callback running in the sole RX producer/progress domain must
   *          not submit a BLOCK read in that same domain; defer it to another execution
   *          context. / 运行于唯一 RX producer/推进域的 completion callback 不得在同一
   *          推进域提交 BLOCK 读取；必须转交其他执行上下文。
   * @warning Calls to this operator and ClearQueuedData() are one logical SPSC consumer
   *          and must be caller-serialized. A previously published request still returns
   *          BUSY; overlapping request publication is outside the contract.
   * @warning 本操作与 ClearQueuedData() 同属一个 SPSC consumer，必须由调用方串行化。
   *          已发布请求仍会返回 BUSY；请求发布过程彼此重叠不在契约内。
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
   * 该接口只丢弃当前软件 RX 队列中已经排队的字节，不参与 backend teardown，也不会
   * 失败完成挂起读请求。若存在正在推进的读请求，则返回 BUSY。
   * This API only discards bytes already queued in the software RX queue. It does not
   * participate in backend teardown and does not fail-complete an in-flight read.
   * Returns BUSY when a read request is currently in progress.
   *
   * @note After this call claims CLAIMED, it owns the current software-queue snapshot
   *       until return. Bytes that arrive after the snapshot may remain queued for a
   *       later reader/clear call.
   * @note 本次调用 claim `CLAIMED` 后，在返回前独占当前软件队列快照；快照之后新到达
   *       的字节可以留给后续读取或下次清队列。
   * @warning Ordinary reads and `ClearQueuedData()` are operations of the same logical
   *          SPSC consumer and must not overlap. `CLAIMED` coordinates this consumer
   *          with the producer's `ReadQueue::Publish()` path; it does not turn
   *          ordinary read/clear calls into multiple concurrent consumers.
   * @warning 普通读取与 `ClearQueuedData()` 同属一个 SPSC consumer，调用不得重叠。
   *          `CLAIMED` 只协调该 consumer 与 producer 的
   *          `ReadQueue::Publish()` 路径，并不会让普通读取和清队列变成可并发的多个
   *          consumer。
   *
   * @param in_isr 是否在 ISR 上下文 / Whether running in ISR context
   * @return `OK` 表示本次清队列成功完成；`BUSY` 表示当前有读请求占有该端口。
   *         `OK` means the clear operation completed; `BUSY` means an active read still
   *         owns this port.
   */
  [[nodiscard]] ErrorCode ClearQueuedData(bool in_isr = false);
};

}  // namespace LibXR
