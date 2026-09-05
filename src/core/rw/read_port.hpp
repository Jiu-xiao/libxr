#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "operation.hpp"
#include "queue.hpp"

namespace LibXR
{

class Pipe;

/**
 * @brief Queue-backed read endpoint.
 * @brief 基于队列的读端口。
 */
class ReadPort
{
 private:
  enum class Phase : uint32_t
  {
    IDLE = 0U,
    CLAIMED = 1U,
    PENDING = 2U,
    CLAIMED_WITH_WAITER = 3U,
    BLOCK_CLAIMED = 4U,
  };

  static constexpr uint32_t EVENT_BIT = 1U << 31U;
  static constexpr uint32_t PHASE_MASK = EVENT_BIT - 1U;

  struct Request
  {
    RawData data;
    ReadOperation op;
  };

  static Phase GetPhase(uint32_t state) { return static_cast<Phase>(state & PHASE_MASK); }

  static uint32_t WithPhase(uint32_t state, Phase phase)
  {
    return (state & EVENT_BIT) | static_cast<uint32_t>(phase);
  }

  static bool HasEvent(uint32_t state) { return (state & EVENT_BIT) != 0U; }

  [[nodiscard]] bool TryClaimIdle();
  [[nodiscard]] bool HasEnough(size_t available, size_t requested) const;
  void ReleaseClaimed(bool in_isr);
  [[nodiscard]] bool ClaimBlockCompletion();
  void ReleaseBlockCompletion(bool in_isr);
  void PublishProduced(bool in_isr);
  void NotifyDataAvailable(bool in_isr);
  void ProcessPendingReads(bool in_isr);
  void CompleteClaimedRead(bool in_isr);
  void CompleteClaimedBlock(bool in_isr);
  [[nodiscard]] ErrorCode WaitForBlock(ReadOperation& op);
  void BindQueue(SPSCQueue<uint8_t>* queue);

  friend class Pipe;

 public:
  /**
   * @brief Short-lived RX producer scope.
   * @brief RX producer 的短生命周期 scope。
   *
   * One scope may use PushBatch or PushWithWriter repeatedly and must end with
   * one explicit Publish. The scope is externally serialized per ReadPort.
   */
  class ReadQueue
  {
   public:
    ReadQueue(const ReadQueue&) = delete;
    ReadQueue& operator=(const ReadQueue&) = delete;
    ReadQueue(ReadQueue&&) = delete;
    ReadQueue& operator=(ReadQueue&&) = delete;
    ~ReadQueue();

    [[nodiscard]] ErrorCode PushBatch(const uint8_t* data, size_t size);

    template <typename Writer>
    [[nodiscard]] size_t PushWithWriter(size_t limit, Writer&& writer)
    {
      DEV_ASSERT_FROM_CALLBACK(!finished_, in_isr_);

      const size_t produced = port_.queue_data_->ProduceWithWriter(
          limit,
          [&](void* first, size_t first_size, void* second, size_t second_size) -> size_t
          {
            const size_t produced = writer(static_cast<uint8_t*>(first), first_size,
                                           static_cast<uint8_t*>(second), second_size);
            REQUIRE_FROM_CALLBACK(produced <= first_size + second_size, in_isr_);
            return produced;
          });
      dirty_ = dirty_ || (produced != 0U);
      return produced;
    }

    [[nodiscard]] size_t EmptySize() const;
    [[nodiscard]] size_t Capacity() const;

    /**
     * @brief Publish the bytes produced by this scope and terminate it.
     * @brief 发布本 scope 生产的字节并终止 scope。
     */
    void Publish();

   private:
    friend class ReadPort;

    ReadQueue(ReadPort& port, bool in_isr) : port_(port), in_isr_(in_isr) {}

    ReadPort& port_;
    const bool in_isr_;
    bool dirty_ = false;
    bool finished_ = false;
  };

  explicit ReadPort(size_t buffer_size = 128U);
  virtual ~ReadPort() = default;

  [[nodiscard]] ReadQueue GetReadQueue(bool in_isr = false);

  [[nodiscard]] size_t EmptySize() const;
  [[nodiscard]] size_t Size() const;
  [[nodiscard]] size_t Capacity() const;
  [[nodiscard]] bool Readable() const;

  ErrorCode operator()(RawData data, ReadOperation& op, bool in_isr = false);

 protected:
  /**
   * @brief Notification after software RX bytes are consumed.
   * @brief 软件 RX 字节被消费后的通知。
   */
  virtual void OnReadQueueSpaceAvailable(bool) {}

 public:
  /**
   * @brief Discard currently queued RX bytes.
   * @brief 丢弃当前软件队列中的 RX 字节。
   *
   * This is a normal consumer-side SPSC discard. It may overlap the single
   * producer; bytes racing the tail snapshot are not promised to survive.
   */
  [[nodiscard]] ErrorCode ClearQueuedData(bool in_isr = false);

 private:
  SPSCQueue<uint8_t>* queue_data_ = nullptr;
  std::atomic<uint32_t> state_{static_cast<uint32_t>(Phase::IDLE)};
  Request info_{};
  ErrorCode block_result_ = ErrorCode::OK;
};

}  // namespace LibXR
