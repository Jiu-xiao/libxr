#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

#ifdef LIBXR_TEST_BUILD
class WritePortTestAccess
{
 public:
  static void SetStreamPublicationHook(void (*hook)());
};
#endif

/**
 * @brief 拥有写队列、请求边界和完成状态的写端口 / Write endpoint owning
 * queued bytes, request boundaries, and completion state
 *
 * producer 只向队尾发布完整请求。唯一的 backend consumer 为每次 owner turn 构造
 * 一个短命 `WriteQueue`，最多接管当前请求和下一请求。`PopWithWriter()` 返回前，
 * writer 必须已经复制字节、写入 FIFO/fd，或发布一个持久 READY 缓冲；不得保留
 * ring span 指针。 / Producers publish complete requests at the queue tail. The sole
 * backend consumer creates one short-lived `WriteQueue` per owner turn and may accept at
 * most the current request plus the next request. Before `PopWithWriter()` returns, its
 * writer must have copied the bytes, written them to a FIFO/fd, or published a persistent
 * READY buffer; ring-span pointers must not escape the call.
 */
class WritePort
{
 public:
  /**
   * @brief 一次 backend owner turn 的有界写队列 / Bounded write queue for one
   * backend owner turn
   *
   * Port 在构造时提供一致的 `front_size` 和 `next_size`。本对象最多接管这两个
   * 请求，析构时一次性推进私有 metadata/count，再按 FIFO 发布完成。 /
   * The Port supplies a consistent `front_size` and `next_size` at construction. This
   * object can accept at most those two requests. Its destructor advances private
   * metadata/count once and then publishes completions in FIFO order.
   */
  class WriteQueue
  {
   public:
    const size_t front_size;  ///< 当前请求未接管字节 / Unaccepted front remainder.
    const size_t next_size;   ///< 下一完整请求字节；无则为 0 / Full next request or 0.

    WriteQueue(const WriteQueue&) = delete;
    WriteQueue& operator=(const WriteQueue&) = delete;
    WriteQueue(WriteQueue&&) = delete;
    WriteQueue& operator=(WriteQueue&&) = delete;
    ~WriteQueue() noexcept;

    /**
     * @brief 通过一次双 span writer 接管前缀 / Accept a prefix through one
     * two-span writer call
     * @tparam Writer 签名为 `size_t(const uint8_t*, size_t, const uint8_t*, size_t)`
     * @param limit 本次最多提供给 writer 的字节 / Maximum bytes offered this call
     * @param writer 返回已持久接管的前缀大小 / Returns the durably accepted prefix
     * @return writer 实际接管并出队的字节数 / Bytes accepted and dequeued
     * @note 多次调用的总量仍不会超过 `front_size + next_size`。writer 或 completion
     * 引起的递归 doorbell 必须由 backend execution policy 合并。 / The sum across
     * calls never exceeds `front_size + next_size`. A recursive doorbell caused by the
     * writer or completion must be coalesced by the backend execution policy.
     */
    template <typename Writer>
    size_t PopWithWriter(size_t limit, Writer&& writer)
    {
      if (limit == 0U || failed_front_)
      {
        return 0U;
      }

      const size_t offered = std::min(limit, front_size + next_size - popped_size_);
      if (offered == 0U)
      {
        return 0U;
      }

      const size_t accepted = port_.MutableDataQueue().ConsumeWithReader(
          offered, std::forward<Writer>(writer));
      popped_size_ += accepted;
      return accepted;
    }

    /**
     * @brief 复制并接管一个有界前缀 / Copy and accept one bounded prefix
     * @param data 接收缓冲区；为 nullptr 时丢弃字节 / Destination; null discards bytes
     * @param limit 最多复制的字节数 / Maximum bytes to copy
     * @return 实际复制并出队的字节数 / Bytes copied and dequeued
     */
    size_t PopBatch(uint8_t* data, size_t limit)
    {
      size_t offset = 0U;
      return PopWithWriter(limit,
                           [data, &offset](const uint8_t* first, size_t first_size,
                                           const uint8_t* second, size_t second_size)
                           {
                             if (data != nullptr)
                             {
                               std::memcpy(data + offset, first, first_size);
                               offset += first_size;
                               if (second_size != 0U)
                               {
                                 std::memcpy(data + offset, second, second_size);
                                 offset += second_size;
                               }
                             }
                             return first_size + second_size;
                           });
    }

    /**
     * @brief 失败并丢弃当前请求的未接管部分 / Fail and discard the current
     * request's unaccepted remainder
     * @param error 发布给当前 Operation 的失败结果 / Failure for the current Operation
     * @return 成功记录失败时为 true；本 scope 已接管字节或无 front 时为 false /
     * True when failure was recorded; false after this scope accepted bytes or with no
     * front request
     * @note fresh scope 可以失败先前 partial front 的剩余部分。 / A fresh scope may
     * fail the remainder of a previously partial front.
     */
    bool FailFront(ErrorCode error);

   private:
    friend class WritePort;

    WriteQueue(WritePort& port, bool in_isr, size_t front, size_t next)
        : front_size(front), next_size(next), port_(port), in_isr_(in_isr)
    {
    }

    WritePort& port_;
    const bool in_isr_;
    size_t popped_size_ = 0U;
    ErrorCode front_result_ = ErrorCode::OK;
    bool failed_front_ = false;
  };

  WriteFun write_fun_ =
      nullptr;  ///< Driver/backend progress doorbell. 底层驱动或后端推进 doorbell。

  /**
   * @brief 构造写端口 / Construct a write port
   * @param queue_size 最多排队请求数 / Maximum queued requests
   * @param buffer_size payload 字节容量 / Payload byte capacity
   * @note 包含动态内存分配 / Contains dynamic allocation.
   */
  WritePort(size_t queue_size = 3U, size_t buffer_size = 128U);

  /** @return payload 队列当前空闲字节 / Current free payload bytes. */
  size_t EmptySize();

  /** @return payload 队列当前已用字节 / Current queued payload bytes. */
  size_t Size();

  /** @return payload 队列总容量 / Total payload capacity. */
  size_t Capacity() const;

  /** @return 已绑定 backend doorbell 时为 true / True when a backend doorbell is bound.
   */
  bool Writable();

  /**
   * @brief 绑定 backend doorbell / Bind the backend doorbell
   * @param fun 持久写推进入口 / Persistent write-progress entry
   * @return 当前端口 / This port
   */
  WritePort& operator=(WriteFun fun);

  /**
   * @brief 构造一次 backend owner turn 的写队列 / Construct the write queue for one
   * backend owner turn
   * @param in_isr completion 是否在 ISR 上下文发布 / Whether completions run in ISR
   * context
   * @return 最多包含 front + next 的短命队列 / Short-lived front-plus-next queue
   * @pre 由唯一、已串行化的 backend consumer 调用；必须在 owner/gate 释放前析构。 /
   * Called by the sole serialized backend consumer and destroyed before releasing its
   * owner/gate.
   */
  WriteQueue GetWriteQueue(bool in_isr = false);

  /**
   * @brief 在 producer admission 内二次确认队列稳定为空并执行短动作 /
   * Recheck stable queue idle under producer admission and run a short action
   * @tparam Action 无参数短动作 / Nullary short action
   * @param action 例如 FIFO flush 或 USB ZLP / For example a FIFO flush or USB ZLP
   * @param in_isr 是否在 ISR 上下文 / Whether called in ISR context
   * @return 成功获得 admission 且私有 request 队列仍为空时为 true / True when
   * admission was acquired and the private request queue remained empty
   * @pre backend 已持有唯一 consumer owner，且已确认私有硬件槽为空 / The
   * backend already owns the sole consumer and has checked its private hardware slots.
   */
  template <typename Action>
  bool TryRunWhenWriteQueueIdle(Action&& action, bool in_isr = false)
  {
    if (!TryAcquireOwner(false))
    {
      return false;
    }

    const bool idle = queue_requests_->Size() == 0U;
    if (idle)
    {
      Action& action_ref = action;
      action_ref();
    }
    ReleaseOwner(in_isr);
    return idle;
  }

  /**
   * @brief 提交一次写入 / Submit one write
   * @param data 输入字节 / Input bytes
   * @param op 完成方式 / Completion operation
   * @param in_isr 是否从 ISR 提交 / Whether submitted from ISR context
   * @return 提交结果；成功入队返回 `OK` / Admission result; `OK` after successful
   * publication
   * @warning BLOCK 不得从 ISR 调用 / BLOCK is forbidden in ISR context.
   * @warning A completion callback running in the sole backend progress domain must not
   *          submit a BLOCK operation in that same domain; defer it to another execution
   *          context. / 运行于唯一 backend 推进域的 completion callback 不得在同一推进域
   *          提交 BLOCK 操作；必须转交其他执行上下文。
   */
  ErrorCode operator()(ConstRawData data, WriteOperation& op, bool in_isr = false);

  // One logical caller owns a Stream between commits, including a completion-callback
  // handoff after RELEASED. SUBMITTING rejects an older completion that races the
  // buffered-size and Port-publication boundary. This is not a general multi-caller
  // serialization primitive. 两次 Commit 之间由一个逻辑调用方独占 Stream；RELEASED
  // 后可交接给 completion callback。SUBMITTING 拒绝与 buffered-size/Port 发布边界
  // 竞争的旧 completion；本状态不负责串行化一般的多调用方并发。
  // A Stream carrying a BLOCK operation follows the same progress-domain restriction as
  // WritePort::operator(). 使用 BLOCK operation 的 Stream 同样受上述推进域限制。
  class Stream
  {
   public:
    Stream(LibXR::WritePort* port, LibXR::WriteOperation op);
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) = delete;
    Stream& operator=(Stream&&) = delete;
    ~Stream();

    [[nodiscard]] ErrorCode Write(ConstRawData data);
    [[nodiscard]] ErrorCode Write(std::string_view text)
    {
      return Write(ConstRawData{text.data(), text.size()});
    }
    Stream& operator<<(const ConstRawData& data);
    ErrorCode Commit();
    [[nodiscard]] ErrorCode Acquire();
    [[nodiscard]] size_t EmptySize() const;

   private:
    friend class WritePort;

    [[nodiscard]] ErrorCode SubmitBuffered();

    LibXR::WritePort* port_;
    LibXR::WriteOperation op_;
    size_t buffered_size_ = 0U;

    enum class StreamState : uint32_t
    {
      RELEASED,
      OWNED,
      SUBMITTING,
    };

    std::atomic<StreamState> state_{StreamState::RELEASED};
  };

 private:
  struct Request
  {
    size_t size;
    WriteOperation op;
  };

  struct DeferredRequest
  {
    ConstRawData data;
    WriteOperation op;
  };

  // Phase owns producer/deferred publication. ActiveState tracks the one synchronous
  // BLOCK caller's wait and buffer-lifetime handoff across deferred copy and published
  // completion. Their Cartesian product avoids a second request identity. Phase 负责
  // producer/deferred 发布；ActiveState 跟踪唯一同步 BLOCK caller 的等待 与 buffer
  // 生命周期交接，覆盖 deferred copy 与已发布 completion。二者的组合 不需要第二个 request
  // identity。
  enum class Phase : uint32_t
  {
    FREE = 0U,
    OWNER = 1U,
    WAITING = 2U,
  };

  enum class ActiveState : uint32_t
  {
    NONE = 0U,
    WAITING = 1U,
    CLAIMED = 2U,
    DETACHED = 3U,
  };

  static constexpr uint32_t PHASE_MASK = 0x3U;
  static constexpr uint32_t ACTIVE_SHIFT = 2U;
  static constexpr uint32_t ACTIVE_MASK = 0x3U << ACTIVE_SHIFT;
  static constexpr uint32_t KICK = 1U << 4U;

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

  static constexpr bool HasKick(uint32_t state) { return (state & KICK) != 0U; }

  SPSCQueue<Request>* const queue_requests_;
  SPSCQueue<uint8_t>* const queue_data_;

#ifdef LIBXR_TEST_BUILD
  friend class WritePortTestAccess;
  static std::atomic<void (*)()> stream_publication_hook_;
#endif

  std::atomic<uint32_t> state_{State(Phase::FREE, ActiveState::NONE)};
  std::atomic<size_t> published_request_count_{0U};

  DeferredRequest deferred_request_{};
  ErrorCode block_result_ = ErrorCode::OK;
  size_t front_remaining_ = 0U;  ///< Sole-consumer state. / 唯一 consumer 状态。

  SPSCQueue<uint8_t>& MutableDataQueue() { return *queue_data_; }
  bool TryAcquireOwner(bool allow_detached);
  void ReleaseOwner(bool in_isr);
  void NotifyBackend(bool in_isr);
  ErrorCode DeferBlock(ConstRawData data, WriteOperation& op, bool owns_port);
  ErrorCode WaitForBlock(WriteOperation& op);
  void CopyAndPublishOwned(ConstRawData data, WriteOperation& op, bool in_isr);
  void PublishOwned(size_t size, WriteOperation& op, bool in_isr,
                    Stream* releasing_stream = nullptr);
  void ReleaseBlockClaim();
  void SettleWriteQueue(WriteQueue& queue) noexcept;
  void CompleteRequest(Request& request, ErrorCode result, bool in_isr);
  void ProcessPendingWrites(bool in_isr);
};

}  // namespace LibXR
