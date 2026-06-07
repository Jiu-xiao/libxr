#include "spmc_queue.hpp"

#include <algorithm>
#include <new>

#include "libxr_mem.hpp"

namespace LibXR
{
/**
 * @brief 构造 SPMC 字节队列 / Construct the SPMC byte queue
 * @param element_size 单个 payload 的字节数 / Byte size of one payload
 * @param capacity 队列容量 / Queue capacity
 */
SPMCQueueBase::SPMCQueueBase(size_t element_size, size_t capacity)
    : element_size_(element_size),
      capacity_(capacity),
      payload_stride_(AlignUpChecked(element_size_, alignof(size_t))),
      sequences_(nullptr),
      payloads_(nullptr),
      head_(0),
      tail_(0)
{
  REQUIRE(element_size_ > 0);
  REQUIRE(capacity_ > 0);
  REQUIRE(capacity_ <= static_cast<size_t>(std::numeric_limits<SequenceDiffType>::max()));

  const size_t payload_bytes = MultiplyChecked(payload_stride_, capacity_);
  sequences_ =
      new (std::align_val_t(alignof(SequenceCell)), std::nothrow) SequenceCell[capacity_];
  REQUIRE(sequences_ != nullptr);

  payloads_ = static_cast<std::byte*>(::operator new[](
      payload_bytes, std::align_val_t(PAYLOAD_ALLOC_ALIGN), std::nothrow));
  if (payloads_ == nullptr)
  {
    delete[] sequences_;
    sequences_ = nullptr;
  }
  REQUIRE(payloads_ != nullptr);

  for (size_t index = 0; index < capacity_; ++index)
  {
    sequences_[index].value.store(static_cast<SequenceType>(index),
                                  std::memory_order_relaxed);
  }
}

/**
 * @brief 析构 SPMC 字节队列 / Destroy the SPMC byte queue
 */
SPMCQueueBase::~SPMCQueueBase()
{
  ::operator delete[](payloads_, std::align_val_t(PAYLOAD_ALLOC_ALIGN));
  delete[] sequences_;
}

/**
 * @brief 按字节入队一个 payload / Enqueue one payload by bytes
 * @param value 指向待入队 payload 的指针 / Pointer to the payload to enqueue
 */
ErrorCode SPMCQueueBase::PushBytes(const void* value) { return PushBatchBytes(value, 1); }

/**
 * @brief 按字节批量入队多个 payload / Enqueue multiple payloads by bytes
 * @param data 指向 payload 数组的字节指针 / Byte pointer to the payload array
 * @param count payload 个数 / Number of payloads
 */
ErrorCode SPMCQueueBase::PushBatchBytes(const void* data, size_t count)
{
  if (count == 0U)
  {
    return ErrorCode::OK;
  }
  if (data == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (count > capacity_)
  {
    return ErrorCode::FULL;
  }

  const SequenceType position = tail_.load(std::memory_order_relaxed);
  const SequenceType head_snapshot = head_.load(std::memory_order_acquire);
  const SequenceType used = position - head_snapshot;
  if (used > capacity_ || capacity_ - used < count)
  {
    return ErrorCode::FULL;
  }

  const auto* src = static_cast<const std::byte*>(data);
  for (size_t index = 0; index < count; ++index)
  {
    const SequenceType slot_position = position + index;
    SequenceCell& slot = sequences_[slot_position % capacity_];
    const SequenceType sequence = slot.value.load(std::memory_order_acquire);
    const SequenceDiffType diff = static_cast<SequenceDiffType>(sequence - slot_position);
    if (diff != 0)
    {
      return ErrorCode::FULL;
    }
  }

  for (size_t index = 0; index < count; ++index)
  {
    const SequenceType slot_position = position + index;
    SequenceCell& slot = sequences_[slot_position % capacity_];
    LibXR::Memory::FastCopy(PayloadPtr(slot_position % capacity_),
                            src + index * element_size_, element_size_);
    slot.value.store(slot_position + 1, std::memory_order_release);
  }

  tail_.store(position + static_cast<SequenceType>(count), std::memory_order_release);
  return ErrorCode::OK;
}

/**
 * @brief 按字节出队一个 payload / Dequeue one payload by bytes
 * @param value 用于接收 payload 的缓冲区；传 `nullptr` 时仅丢弃
 *        / Buffer receiving the payload; pass `nullptr` to discard only
 */
ErrorCode SPMCQueueBase::PopBytes(void* value) { return PopBatchBytes(value, 1); }

/**
 * @brief 按字节批量出队多个 payload / Dequeue multiple payloads by bytes
 * @param data 用于接收 payload 的字节缓冲区；传 `nullptr` 时仅丢弃
 *        / Byte buffer receiving payloads; pass `nullptr` to discard only
 * @param count payload 个数 / Number of payloads
 */
ErrorCode SPMCQueueBase::PopBatchBytes(void* data, size_t count)
{
  if (count == 0U)
  {
    return ErrorCode::OK;
  }
  if (count > capacity_)
  {
    return ErrorCode::EMPTY;
  }

  SequenceType position = head_.load(std::memory_order_relaxed);
  while (true)
  {
    const SequenceType tail_snapshot = tail_.load(std::memory_order_acquire);
    const SequenceType available = tail_snapshot - position;
    if (available < count || available > capacity_)
    {
      return ErrorCode::EMPTY;
    }

    if (head_.compare_exchange_weak(position, position + static_cast<SequenceType>(count),
                                    std::memory_order_relaxed, std::memory_order_relaxed))
    {
      break;
    }
  }

  auto* dst = static_cast<std::byte*>(data);
  for (size_t index = 0; index < count; ++index)
  {
    const SequenceType slot_position = position + index;
    SequenceCell& slot = sequences_[slot_position % capacity_];
    const SequenceType expected_ready = slot_position + 1;
    const SequenceType sequence = slot.value.load(std::memory_order_acquire);
    ASSERT(sequence == expected_ready);

    if (dst != nullptr)
    {
      LibXR::Memory::FastCopy(dst + index * element_size_,
                              PayloadPtr(slot_position % capacity_), element_size_);
    }
    slot.value.store(slot_position + static_cast<SequenceType>(capacity_),
                     std::memory_order_release);
  }

  return ErrorCode::OK;
}

/**
 * @brief 重置队列状态 / Reset the queue state
 */
void SPMCQueueBase::Reset()
{
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  for (size_t index = 0; index < capacity_; ++index)
  {
    sequences_[index].value.store(static_cast<SequenceType>(index),
                                  std::memory_order_relaxed);
  }
}

/**
 * @brief 获取并发快照下的当前元素数 / Get the current approximate element count
 * @return 并发快照下的元素数，范围被钳在 `[0, MaxSize()]`
 *         Approximate element count from a concurrent snapshot, clamped to
 *         `[0, MaxSize()]`
 */
size_t SPMCQueueBase::Size() const
{
  const SequenceType head_snapshot = head_.load(std::memory_order_acquire);
  const SequenceType tail_snapshot = tail_.load(std::memory_order_acquire);
  const SequenceType used = tail_snapshot - head_snapshot;
  return (used <= capacity_) ? used : capacity_;
}

/**
 * @brief 获取指定槽位 payload 起始地址 / Get one slot payload address
 * @param index 槽位下标 / Slot index
 */
void* SPMCQueueBase::PayloadPtr(size_t index)
{
  return payloads_ + index * payload_stride_;
}

/**
 * @brief 获取指定槽位 payload 起始地址（只读） / Get one slot payload address (const)
 * @param index 槽位下标 / Slot index
 */
const void* SPMCQueueBase::PayloadPtr(size_t index) const
{
  return payloads_ + index * payload_stride_;
}

/**
 * @brief 向上对齐到指定粒度 / Align one byte count upward to the target granularity
 * @param value 待对齐字节数 / Byte count to align
 * @param align 目标对齐粒度 / Target alignment granularity
 */
size_t SPMCQueueBase::AlignUpChecked(size_t value, size_t align)
{
  REQUIRE(align > 0);
  REQUIRE(value <= std::numeric_limits<size_t>::max() - (align - 1));
  return ((value + align - 1) / align) * align;
}

/**
 * @brief 安全地计算两个字节数的乘积 / Safely multiply two byte counts
 * @param lhs 左操作数 / Left operand
 * @param rhs 右操作数 / Right operand
 * @return 乘积结果 / Product result
 */
size_t SPMCQueueBase::MultiplyChecked(size_t lhs, size_t rhs)
{
  if (lhs == 0 || rhs == 0)
  {
    return 0;
  }

  REQUIRE(lhs <= std::numeric_limits<size_t>::max() / rhs);
  return lhs * rhs;
}
}  // namespace LibXR
