#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

#include "libxr_def.hpp"

namespace LibXR
{
/**
 * @class SPMCQueueBase
 * @brief 单生产者多消费者字节队列 / Single-producer multiple-consumer byte queue
 *
 * 该队列允许一个生产者按 FIFO 顺序发布固定大小字节 payload，多个消费者通过
 * CAS 抢占出队位置。每个槽位带独立序号，消费者成功 claim 后先复制 payload，
 * 再释放槽位，因此生产者不会覆盖正在被读取的槽。
 *
 * This queue lets one producer publish fixed-size byte payloads in FIFO order
 * while multiple consumers claim dequeue positions with CAS. Each slot has an
 * independent sequence value, so a claimed slot is copied before it is released
 * back to the producer.
 */
class SPMCQueueBase
{
 public:
  using SequenceType = size_t;  ///< 单调递增的逻辑序号类型 / Monotonic sequence type.
  using SequenceDiffType =
      std::make_signed_t<SequenceType>;  ///< 序号差值类型 / Signed sequence-delta type.

  SPMCQueueBase(size_t element_size, size_t capacity);
  ~SPMCQueueBase();

  ErrorCode PushBytes(const void* value);
  ErrorCode PopBytes(void* value = nullptr);
  ErrorCode PushBatchBytes(const void* data, size_t count);
  ErrorCode PopBatchBytes(void* data, size_t count);
  void Reset();
  [[nodiscard]] size_t Size() const;
  [[nodiscard]] size_t EmptySize() const { return capacity_ - Size(); }
  [[nodiscard]] size_t MaxSize() const { return capacity_; }
  [[nodiscard]] size_t ElementSize() const { return element_size_; }

 private:
  struct alignas(LibXR::CONCURRENCY_ALIGNMENT) SequenceCell
  {
    std::atomic<SequenceType> value;
  };

  [[nodiscard]] void* PayloadPtr(size_t index);
  [[nodiscard]] const void* PayloadPtr(size_t index) const;
  [[nodiscard]] static size_t AlignUpChecked(size_t value, size_t align);
  [[nodiscard]] static size_t MultiplyChecked(size_t lhs, size_t rhs);
  static constexpr size_t PAYLOAD_ALLOC_ALIGN =
      std::max(alignof(size_t), alignof(std::max_align_t));

  SPMCQueueBase(const SPMCQueueBase&);
  SPMCQueueBase& operator=(const SPMCQueueBase&);
  SPMCQueueBase(SPMCQueueBase&&);
  SPMCQueueBase& operator=(SPMCQueueBase&&);

  const size_t element_size_;
  const size_t capacity_;
  const size_t payload_stride_;
  SequenceCell* sequences_;
  std::byte* payloads_;

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType> head_;
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType> tail_;
};
}  // namespace LibXR
