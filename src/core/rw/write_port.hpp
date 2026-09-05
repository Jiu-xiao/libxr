#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

class Pipe;

/**
 * @brief Queue-backed write endpoint.
 * @brief 基于队列的写端口。
 */
class WritePort
{
 private:
  enum class Phase : uint32_t
  {
    IDLE = 0U,
    LOCKED = 1U,
    BLOCK_WAITING = 2U,
    BLOCK_CLAIMED = 3U,
    BLOCK_DETACHED = 4U,
    BLOCK_RETIRE_WAITING = 5U,
  };

  static constexpr uint32_t PHASE_BITS = 3U;
  static constexpr uint32_t PHASE_MASK = (1U << PHASE_BITS) - 1U;
  static constexpr uint32_t RELEASED_INCREMENT = 1U << PHASE_BITS;
  static constexpr uint32_t MAX_RELEASED_REQUESTS = UINT32_MAX >> PHASE_BITS;

  static_assert(static_cast<uint32_t>(Phase::BLOCK_RETIRE_WAITING) <= PHASE_MASK);

  struct Request
  {
    size_t size;
    WriteOperation op;
  };

  static_assert(std::is_trivially_copyable_v<Request>);
  static_assert(std::is_trivially_destructible_v<Request>);

  static constexpr uint32_t MakeState(Phase phase, uint32_t released)
  {
    return (released << PHASE_BITS) | static_cast<uint32_t>(phase);
  }

  static Phase GetPhase(uint32_t state) { return static_cast<Phase>(state & PHASE_MASK); }

  static uint32_t GetReleasedCount(uint32_t state) { return state >> PHASE_BITS; }

  static uint32_t WithPhase(uint32_t state, Phase phase)
  {
    return (state & ~PHASE_MASK) | static_cast<uint32_t>(phase);
  }

  static size_t ValidateQueueSize(size_t queue_size)
  {
    REQUIRE(queue_size <= MAX_RELEASED_REQUESTS);
    return queue_size;
  }

  [[nodiscard]] bool IsAdmissionMode() const { return queue_requests_ == nullptr; }
  [[nodiscard]] Phase LoadPhase() const
  {
    return GetPhase(state_.load(std::memory_order_acquire));
  }
  [[nodiscard]] bool TryClaimProducer();
  [[nodiscard]] bool TryRegisterRetirementWait(WriteOperation& op);
  [[nodiscard]] ErrorCode WaitForRetirement(WriteOperation& op);
  [[nodiscard]] ErrorCode CommitQueued(size_t size, WriteOperation& op, bool in_isr);
  [[nodiscard]] ErrorCode CommitAdmission(size_t size, WriteOperation& op, bool in_isr);
  [[nodiscard]] ErrorCode WaitForBlock(WriteOperation& op);
  void ReleaseProducer(Phase next, bool in_isr);
  void PublishQueuedRequest(Phase next, bool in_isr);
  void DecrementReleasedRequest(bool in_isr);
  void NotifyBackend(bool in_isr);
  void SettleWriteQueue(size_t accepted, ErrorCode result, bool in_isr) noexcept;
  void CompleteRequest(Request& request, ErrorCode result, bool in_isr);

  friend class Pipe;

 public:
  /**
   * @brief Short-lived view of one committed front request.
   * @brief 一个已提交队头请求的短生命周期视图。
   *
   * One consumer owner must serialize all WriteQueue scopes for a port. Accepted
   * bytes must be in stable backend storage before scope destruction because the
   * destruction path may complete the operation and invoke user code.
   */
  class WriteQueue
  {
   public:
    WriteQueue(const WriteQueue&) = delete;
    WriteQueue& operator=(const WriteQueue&) = delete;
    WriteQueue(WriteQueue&&) = delete;
    WriteQueue& operator=(WriteQueue&&) = delete;
    ~WriteQueue() noexcept;

    [[nodiscard]] size_t AvailableSize() const
    {
      ASSERT_FROM_CALLBACK(popped_size_ <= front_size_, in_isr_);
      return front_size_ - popped_size_;
    }

    [[nodiscard]] bool Empty() const { return AvailableSize() == 0U; }

    /**
     * @brief Accept the complete current front into stable backend storage.
     * @brief 将当前完整队头接收到稳定的后端存储。
     *
     * Size, destination, and queue state are backend invariants; impossible
     * arguments are diagnosed internally rather than returned as a recoverable
     * public error.
     */
    void PopAll(uint8_t* destination);

    /**
     * @brief Accept a bounded prefix through one two-span writer.
     * @brief 通过一次双 span writer 接收有界前缀。
     *
     * The callback returns the number of bytes durably accepted. Only that prefix
     * leaves the queue; zero is a valid no-progress result and keeps the front.
     */
    template <typename Writer>
    size_t PopWithWriter(size_t limit, Writer&& writer)
    {
      BeginAction();
      const size_t offered = std::min(limit, AvailableSize());
      if (offered == 0U)
      {
        return 0U;
      }

      const size_t accepted = port_.queue_data_->ConsumeWithReader(
          offered,
          [&](const uint8_t* first, size_t first_size, const uint8_t* second,
              size_t second_size) -> size_t
          {
            const size_t accepted = writer(first, first_size, second, second_size);
            REQUIRE_FROM_CALLBACK(accepted <= first_size + second_size, in_isr_);
            return accepted;
          });
      popped_size_ += accepted;
      return accepted;
    }

    /**
     * @brief Drop the current front remainder as an unreplayable backend failure.
     * @brief 将当前队头剩余部分按不可重放后端失败丢弃。
     */
    void FailFront(ErrorCode reason);

   private:
    friend class WritePort;

    void BeginAction()
    {
      REQUIRE_FROM_CALLBACK(!action_used_, in_isr_);
      action_used_ = true;
    }

    WriteQueue(WritePort& port, bool in_isr, size_t front_size)
        : port_(port), in_isr_(in_isr), front_size_(front_size)
    {
    }

    WritePort& port_;
    const bool in_isr_;
    const size_t front_size_;
    size_t popped_size_ = 0U;
    ErrorCode settlement_result_ = ErrorCode::OK;
    bool action_used_ = false;
  };

  /**
   * @brief Stream batch facade.
   * @brief 流式批次门面。
   */
  class Stream
  {
   public:
    Stream(WritePort* port, WriteOperation op);
    ~Stream();

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) = delete;
    Stream& operator=(Stream&&) = delete;

    [[nodiscard]] ErrorCode Write(ConstRawData data);
    [[nodiscard]] ErrorCode Write(std::string_view text)
    {
      return Write(ConstRawData{text.data(), text.size()});
    }

    Stream& operator<<(const ConstRawData& data);
    [[nodiscard]] ErrorCode Commit();
    [[nodiscard]] ErrorCode Acquire();

    [[nodiscard]] size_t EmptySize() const
    {
      return owns_port_ && port_ != nullptr && port_->queue_data_ != nullptr
                 ? port_->queue_data_->EmptySize()
                 : 0U;
    }

   private:
    [[nodiscard]] ErrorCode SubmitBuffered();
    void Release();
    void CompleteEmpty();

    WritePort* port_ = nullptr;
    WriteOperation op_;
    size_t buffered_size_ = 0U;
    bool owns_port_ = false;
  };

  /**
   * @brief Construct queued mode when queue_size is positive, or admission mode
   *        when queue_size is zero.
   * @brief queue_size 为正时构造 queued 模式，为零时构造 admission 模式。
   */
  WritePort(size_t queue_size = 3U, size_t buffer_size = 128U);

  [[nodiscard]] size_t EmptySize() const;
  [[nodiscard]] size_t Size() const;
  [[nodiscard]] size_t Capacity() const;
  [[nodiscard]] bool Writable() const;

  WritePort& operator=(WriteFun fun);

  /**
   * @brief Get one committed front request for a serialized backend consumer.
   * @brief 为已串行化的后端 consumer 取得一个已提交队头请求。
   */
  [[nodiscard]] WriteQueue GetWriteQueue(bool in_isr = false);

  ErrorCode operator()(ConstRawData data, WriteOperation& op, bool in_isr = false);

 private:
  SPSCQueue<Request>* queue_requests_;
  SPSCQueue<uint8_t>* queue_data_;
  WriteFun write_fun_ = nullptr;

  std::atomic<uint32_t> state_{MakeState(Phase::IDLE, 0U)};
  ErrorCode block_result_ = ErrorCode::OK;
  size_t front_remaining_ = 0U;
  Semaphore* admission_waiter_ = nullptr;
};

}  // namespace LibXR
