#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR
{
/**
 * @class MPMCQueueCore
 * @brief 有界 MPMC 字节队列内核 / Bounded MPMC byte-queue core
 *
 * 这个内核把并发协议和字节搬运集中在一个非模板实现里，以减少不同 payload
 * 类型各自实例化一整套无锁协议所带来的 flash 膨胀。它只负责搬运固定大小、
 * 默认字宽对齐的字节 payload；类型语义由上层薄包装负责。
 *
 * This core keeps the concurrency protocol and byte-copying logic in one
 * non-template implementation so different payload types do not each instantiate
 * a full copy of the lock-free protocol. It only moves fixed-size, word-aligned
 * byte payloads; type semantics are handled by thin wrappers above it.
 */
class MPMCQueueCore
{
 public:
  using SequenceType = size_t;

  MPMCQueueCore(size_t element_size, size_t capacity);
  ~MPMCQueueCore();

  MPMCQueueCore(const MPMCQueueCore&) = delete;
  MPMCQueueCore& operator=(const MPMCQueueCore&) = delete;
  MPMCQueueCore(MPMCQueueCore&&) = delete;
  MPMCQueueCore& operator=(MPMCQueueCore&&) = delete;

  ErrorCode PushBytes(const void* value);
  ErrorCode PopBytes(void* value);
  ErrorCode PopBytes();

  [[nodiscard]] size_t MaxSize() const { return capacity_; }
  /**
   * @brief 获取并发快照下的当前元素数 / Get the current approximate element count
   *
   * @note 该值是近似快照：
   *       - 在并发入队/出队时，该值可能已经过期；
   *       - 这里按 `SequenceType` 的模差值估算已用槽数，再钳到 `[0, MaxSize()]`，
   *         因此即使极端长寿命下序号回绕后，它仍然只是近似值，而不是精确值。
   *       This value is an approximate snapshot:
   *       - it may already be stale while producers/consumers are progressing;
   *       - it is computed from modular `SequenceType` differences and then
   *         clamped to `[0, MaxSize()]`, so it remains approximate rather than
   *         exact even after very long-lived sequence wraparound.
   */
  [[nodiscard]] size_t Size() const;
  [[nodiscard]] size_t EmptySize() const { return capacity_ - Size(); }
  [[nodiscard]] size_t ElementSize() const { return element_size_; }

 private:
  struct alignas(LibXR::CONCURRENCY_ALIGNMENT) SequenceCell
  {
    std::atomic<SequenceType> value;
  };

  [[nodiscard]] void* PayloadPtr(size_t index);
  [[nodiscard]] const void* PayloadPtr(size_t index) const;
  [[nodiscard]] static size_t AlignUp(size_t value, size_t align);

  const size_t element_size_;
  const size_t capacity_;
  const size_t payload_stride_;
  const size_t payload_alloc_align_;
  SequenceCell* sequences_;
  std::byte* payloads_;

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType> head_;
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType> tail_;
};
}  // namespace LibXR
