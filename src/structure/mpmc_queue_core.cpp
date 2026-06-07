#include "mpmc_queue_core.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace LibXR
{
/**
 * @brief 构造字节队列内核 / Construct the byte-queue core
 * @param element_size 单个 payload 的字节数 / Byte size of one payload
 * @param capacity 队列容量 / Queue capacity
 */
MPMCQueueCore::MPMCQueueCore(size_t element_size, size_t capacity)
    : element_size_(element_size),
      capacity_(capacity),
      payload_stride_(AlignUp(element_size_, alignof(size_t))),
      payload_alloc_align_(std::max(alignof(size_t), alignof(std::max_align_t))),
      sequences_(new (std::align_val_t(alignof(SequenceCell))) SequenceCell[capacity_]),
      payloads_(static_cast<std::byte*>(
          ::operator new[](payload_stride_ * capacity_,
                           std::align_val_t(payload_alloc_align_)))),
      head_(0),
      tail_(0)
{
  ASSERT(element_size_ > 0);
  REQUIRE(capacity_ > 1);

  for (size_t index = 0; index < capacity_; ++index)
  {
    sequences_[index].value.store(static_cast<SequenceType>(index),
                                  std::memory_order_relaxed);
  }
}

/**
 * @brief 析构字节队列内核 / Destroy the byte-queue core
 */
MPMCQueueCore::~MPMCQueueCore()
{
  ::operator delete[](payloads_, std::align_val_t(payload_alloc_align_));
  delete[] sequences_;
}

/**
 * @brief 按字节入队一个 payload / Enqueue one payload by bytes
 * @param value 指向待入队 payload 的指针 / Pointer to the payload to enqueue
 */
ErrorCode MPMCQueueCore::PushBytes(const void* value)
{
  if (value == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  SequenceType position = tail_.load(std::memory_order_relaxed);

  while (true)
  {
    SequenceCell& slot = sequences_[position % capacity_];
    const SequenceType sequence = slot.value.load(std::memory_order_acquire);

    if (sequence == position)
    {
      if (tail_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed,
                                      std::memory_order_relaxed))
      {
        std::memcpy(PayloadPtr(position % capacity_), value, element_size_);
        slot.value.store(position + 1, std::memory_order_release);
        return ErrorCode::OK;
      }
      continue;
    }

    if (sequence < position)
    {
      return ErrorCode::FULL;
    }

    position = tail_.load(std::memory_order_relaxed);
  }
}

/**
 * @brief 按字节出队一个 payload / Dequeue one payload by bytes
 * @param value 用于接收 payload 的缓冲区 / Buffer that receives the payload
 */
ErrorCode MPMCQueueCore::PopBytes(void* value)
{
  if (value == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  SequenceType position = head_.load(std::memory_order_relaxed);

  while (true)
  {
    SequenceCell& slot = sequences_[position % capacity_];
    const SequenceType sequence = slot.value.load(std::memory_order_acquire);
    const SequenceType expected_ready = position + 1;

    if (sequence == expected_ready)
    {
      if (head_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed,
                                      std::memory_order_relaxed))
      {
        std::memcpy(value, PayloadPtr(position % capacity_), element_size_);
        slot.value.store(position + static_cast<SequenceType>(capacity_),
                         std::memory_order_release);
        return ErrorCode::OK;
      }
      continue;
    }

    if (sequence < expected_ready)
    {
      return ErrorCode::EMPTY;
    }

    position = head_.load(std::memory_order_relaxed);
  }
}

/**
 * @brief 丢弃一个队头 payload / Discard one front payload
 */
ErrorCode MPMCQueueCore::PopBytes()
{
  SequenceType position = head_.load(std::memory_order_relaxed);

  while (true)
  {
    SequenceCell& slot = sequences_[position % capacity_];
    const SequenceType sequence = slot.value.load(std::memory_order_acquire);
    const SequenceType expected_ready = position + 1;

    if (sequence == expected_ready)
    {
      if (head_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed,
                                      std::memory_order_relaxed))
      {
        slot.value.store(position + static_cast<SequenceType>(capacity_),
                         std::memory_order_release);
        return ErrorCode::OK;
      }
      continue;
    }

    if (sequence < expected_ready)
    {
      return ErrorCode::EMPTY;
    }

    position = head_.load(std::memory_order_relaxed);
  }
}

/**
 * @brief 获取并发快照下的当前元素数 / Get the current approximate element count
 * @return 并发快照下的元素数，范围被钳在 `[0, MaxSize()]`
 *         Approximate element count from a concurrent snapshot, clamped to
 *         `[0, MaxSize()]`
 */
size_t MPMCQueueCore::Size() const
{
  const SequenceType head_snapshot = head_.load(std::memory_order_acquire);
  const SequenceType tail_snapshot = tail_.load(std::memory_order_acquire);
  const SequenceType used =
      (tail_snapshot >= head_snapshot) ? (tail_snapshot - head_snapshot) : 0;
  return (used <= capacity_) ? used : capacity_;
}

/**
 * @brief 获取指定槽位 payload 起始地址 / Get the payload base address of one slot
 * @param index 槽位下标 / Slot index
 */
void* MPMCQueueCore::PayloadPtr(size_t index)
{
  return payloads_ + index * payload_stride_;
}

/**
 * @brief 获取指定槽位 payload 起始地址（只读） / Get the payload base address of one slot (const)
 * @param index 槽位下标 / Slot index
 */
const void* MPMCQueueCore::PayloadPtr(size_t index) const
{
  return payloads_ + index * payload_stride_;
}

/**
 * @brief 向上对齐到指定粒度 / Align one byte count upward to the target granularity
 * @param value 待对齐字节数 / Byte count to align
 * @param align 目标对齐粒度 / Target alignment granularity
 */
size_t MPMCQueueCore::AlignUp(size_t value, size_t align)
{
  return ((value + align - 1) / align) * align;
}
}  // namespace LibXR
