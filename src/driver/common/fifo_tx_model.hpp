#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief 将排队记录推进到硬件 FIFO 的 TX-only 模型 / TX-only model that advances
 * queued records into a hardware FIFO
 *
 * `WritePort` 始终是尚未被硬件接受 payload 的唯一持久 owner。模型只保存已接管记录的
 * metadata 和 offset。FIFO 空间、等待下一次空间、UART CONFIG/line-idle、USB packet/ZLP
 * 和 RX 均由后端负责。 / `WritePort` remains the only persistent owner of payload not
 * yet accepted by hardware. The model stores only claimed record metadata and its offset.
 * FIFO availability and wakeup, UART configuration and line idle, USB packet and ZLP
 * handling, and RX remain backend responsibilities.
 *
 * 调用方必须通过同一个 serialized service owner 调用本模型；该 owner 也必须覆盖
 * `DequeueScope` 发布队列空间时可能触发的后端推进。消费者可以在 producer publication
 * 期间接管和搬运数据；只有异步
 * completion publication 需要通过 `WritePort` CAS。被阻塞的 terminal 保留在模型单槽中，
 * producer 释放 admission 后只负责重新 kick 后端。 / Calls must use one serialized
 * service owner, including backend progress triggered while `DequeueScope` publishes
 * queue-space progress. A consumer may claim and move data during producer publication.
 * Only asynchronous completion publication passes through the `WritePort` CAS. A blocked
 * terminal remains in the model's single slot; the producer only re-kicks the backend
 * after releasing admission.
 */
class FifoTxModel
{
 public:
  /** @brief 当前是否允许发布记录终态 / Whether record completion may be published now. */
  enum class CompletionPublication : uint8_t
  {
    ALLOW,
    DEFER,
  };

  /**
   * @brief 绑定由模型消费的 WritePort / Bind the WritePort consumed by this model
   * @param port 生命周期必须覆盖本模型 / Port that must outlive this model
   */
  explicit FifoTxModel(WritePort& port) : port_(port)
  {
    REQUIRE(port_.QueueData() != nullptr);
  }

  FifoTxModel(const FifoTxModel&) = delete;
  FifoTxModel& operator=(const FifoTxModel&) = delete;
  FifoTxModel(FifoTxModel&&) = delete;
  FifoTxModel& operator=(FifoTxModel&&) = delete;

  /** @return 模型是否持有仍需写入 FIFO 的记录 / Whether a record still needs FIFO writes.
   */
  [[nodiscard]] bool HasActiveRecord() const noexcept
  {
    return current_record_.data.size_ != 0U &&
           current_record_offset_ < current_record_.data.size_;
  }

  /** @return 是否有已被硬件接受但尚未发布终态的记录 / Whether an accepted record awaits
   * terminal publication. */
  [[nodiscard]] bool HasPendingCompletion() const noexcept
  {
    return current_record_.data.size_ != 0U &&
           current_record_offset_ == current_record_.data.size_;
  }

  /**
   * @brief 接管下一条排队记录 / Claim the next queued record
   * @param in_isr 当前是否在 ISR / Whether currently in an ISR
   * @return 接管到记录时为 true；队列为空时为 false / True when a record was claimed;
   *         false when the queue was empty
   */
  [[nodiscard]] bool TryClaim(bool in_isr)
  {
    ASSERT(!HasActiveRecord());
    ASSERT(!HasPendingCompletion());
    return TryClaimNextRecord(in_isr);
  }

  /**
   * @brief 通过保证全量接受的 writer 填充 FIFO / Fill a FIFO through an exact writer
   * @tparam Writer `void(const uint8_t*, size_t)` writer 类型 / Writer type
   * @param in_isr 当前是否在 ISR / Whether currently in an ISR
   * @param writable_size 本轮稳定的可写字节预算 / Stable writable-byte budget
   * @param publication 后端当前是否允许发布终态 / Whether terminal publication is allowed
   * @param writer 全量接受连续队列片段的硬件 writer / Exact hardware writer
   * @return 本轮是否已释放记录 / Whether this turn released the record
   */
  template <typename Writer>
  bool FillExact(bool in_isr, size_t writable_size, CompletionPublication publication,
                 Writer&& writer)
  {
    ValidateExactWriter<Writer>();
    ASSERT(HasActiveRecord());

    if (writable_size == 0U)
    {
      return false;
    }

    const size_t accepted = std::min(Remaining(), writable_size);
    FillExactData(in_isr, accepted, std::forward<Writer>(writer));
    return PublishPendingCompletion(in_isr, publication);
  }

  /**
   * @brief 通过调用方 scratch 和允许部分接受的 writer 填充 FIFO / Fill a FIFO through
   * caller scratch storage and a partial writer
   *
   * scratch 仅在本轮调用中保存队首副本，不成为持久 payload owner。模型只按 writer
   * 实际返回的 accepted 字节数出队。 / Scratch holds only a turn-local copy of the queue
   * prefix and never becomes a persistent payload owner. The model dequeues exactly the
   * number of bytes actually accepted by the writer.
   *
   * @tparam Writer `size_t(const uint8_t*, size_t)` writer 类型 / Writer type
   * @param in_isr 当前是否在 ISR / Whether currently in an ISR
   * @param writable_size 本轮最大提交字节数 / Maximum bytes offered this turn
   * @param scratch 本轮临时连续存储 / Turn-local contiguous scratch storage
   * @param publication 后端当前是否允许发布终态 / Whether terminal publication is allowed
   * @param writer 返回实际接受字节数的硬件 writer / Writer returning accepted byte count
   * @return 本轮是否已释放记录 / Whether this turn released the record
   */
  template <typename Writer>
  bool FillWithScratch(bool in_isr, size_t writable_size, RawData scratch,
                       CompletionPublication publication, Writer&& writer)
  {
    static_assert(std::is_invocable_v<Writer&, const uint8_t*, size_t>,
                  "FifoTxModel partial writer has an invalid signature");
    using WriterResult = std::invoke_result_t<Writer&, const uint8_t*, size_t>;
    static_assert(std::is_convertible_v<WriterResult, size_t>,
                  "FifoTxModel partial writer must return accepted byte count");
    ASSERT(HasActiveRecord());

    const size_t offered = std::min({Remaining(), writable_size, scratch.size_});
    if (offered == 0U)
    {
      return false;
    }
    REQUIRE_FROM_CALLBACK(scratch.addr_ != nullptr, in_isr);

    auto* bytes = reinterpret_cast<uint8_t*>(scratch.addr_);
    const ErrorCode peek_result = port_.QueueData()->PeekBatch(bytes, offered);
    REQUIRE_FROM_CALLBACK(peek_result == ErrorCode::OK, in_isr);

    Writer& writer_ref = writer;
    const size_t accepted = static_cast<size_t>(writer_ref(bytes, offered));
    REQUIRE_FROM_CALLBACK(accepted <= offered, in_isr);
    if (accepted == 0U)
    {
      return false;
    }

    {
      auto dequeue = port_.BeginDequeue(in_isr);
      const ErrorCode pop_result = dequeue.DiscardData(accepted);
      REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);
      current_record_offset_ += accepted;
    }
    return PublishPendingCompletion(in_isr, publication);
  }

  /**
   * @brief 由 serialized owner 发布一个暂存终态 / Publish one deferred terminal from the
   * serialized owner
   * @param in_isr 当前是否在 ISR / Whether currently in an ISR
   * @param publication 后端当前是否允许发布终态 / Whether terminal publication is allowed
   * @return 已发布并释放记录时为 true / True when the record was published and released
   */
  bool PublishPendingCompletion(bool in_isr, CompletionPublication publication)
  {
    if (!HasPendingCompletion() || publication != CompletionPublication::ALLOW ||
        !port_.TryPublishBackendCompletion())
    {
      return false;
    }

    WriteInfoBlock completed = TakeCompletedRecord();
    port_.Finish(in_isr, ErrorCode::OK, completed);
    return true;
  }

 private:
  template <typename Writer>
  static constexpr void ValidateExactWriter()
  {
    static_assert(std::is_invocable_v<Writer&, const uint8_t*, size_t>,
                  "FifoTxModel exact writer has an invalid signature");
    using WriterResult = std::invoke_result_t<Writer&, const uint8_t*, size_t>;
    static_assert(std::is_void_v<WriterResult>,
                  "FifoTxModel exact writer must return void");
  }

  void ValidateRecord(const WriteInfoBlock& info, bool in_isr) const
  {
    REQUIRE_FROM_CALLBACK(info.data.size_ > 0U, in_isr);
    REQUIRE_FROM_CALLBACK(info.data.size_ <= port_.QueueData()->Size(), in_isr);
  }

  bool TryClaimNextRecord(bool in_isr)
  {
    auto dequeue = port_.BeginDequeue(in_isr);
    WriteInfoBlock claimed{};
    const ErrorCode pop_result = dequeue.PopInfo(claimed);
    if (pop_result != ErrorCode::OK)
    {
      return false;
    }
    ValidateRecord(claimed, in_isr);
    current_record_ = claimed;
    current_record_offset_ = 0U;

    return true;
  }

  template <typename Writer>
  void FillExactData(bool in_isr, size_t accepted, Writer&& writer)
  {
    auto dequeue = port_.BeginDequeue(in_isr);
    Writer& writer_ref = writer;
    const ErrorCode result = dequeue.PopDataWithReader(
        accepted,
        [&writer_ref](const uint8_t* data, size_t size) -> ErrorCode
        {
          writer_ref(data, size);
          return ErrorCode::OK;
        });
    REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
    current_record_offset_ += accepted;
  }

  [[nodiscard]] size_t Remaining() const noexcept
  {
    ASSERT(HasActiveRecord());
    return current_record_.data.size_ - current_record_offset_;
  }

  WriteInfoBlock TakeCompletedRecord() noexcept
  {
    ASSERT(HasPendingCompletion());
    WriteInfoBlock completed = current_record_;
    current_record_ = {};
    current_record_offset_ = 0U;
    return completed;
  }

  WritePort& port_;
  WriteInfoBlock current_record_{};
  size_t current_record_offset_ = 0U;
};

}  // namespace LibXR
