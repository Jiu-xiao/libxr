#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <new>

#include "libxr_def.hpp"
#include "libxr_mem.hpp"

namespace LibXR
{
/**
 * @class SPSCQueueBase
 * @brief 单生产者单消费者字节队列内核 / Single-producer single-consumer byte-queue core
 *
 * 这个内核只负责 SPSC ring 的索引推进、容量判断与原始字节存储，不包含多消费者
 * 抢占逻辑，也不暴露任何 CAS 路径。上层 `SPSCQueue<Data>` 只负责把具体类型
 * `Data` 映射到这些字节槽位。
 *
 * This core contains only the SPSC ring progression, capacity checks, and raw
 * byte storage. It does not include any multi-consumer claiming logic or expose
 * any CAS-based path. The upper `SPSCQueue<Data>` wrapper is responsible only
 * for mapping the concrete `Data` type onto these byte slots.
 */
class alignas(LibXR::CONCURRENCY_ALIGNMENT) SPSCQueueBase
{
 public:
  using IndexType = size_t;  ///< 环形缓冲区索引类型 / Ring-buffer index type.

  /**
   * @brief 构造 SPSC 字节队列内核 / Construct the SPSC byte-queue core
   * @param element_size 单个 payload 的字节数 / Byte size of one payload
   * @param capacity 队列容量 / Queue capacity
   */
  SPSCQueueBase(size_t element_size, size_t capacity)
      : element_size_(element_size),
        capacity_(capacity),
        payload_stride_(AlignUpChecked(element_size_, alignof(size_t))),
        payloads_(nullptr),
        head_(0),
        tail_(0)
  {
    REQUIRE(element_size_ > 0);
    REQUIRE(capacity_ > 0);
    REQUIRE(capacity_ <= std::numeric_limits<size_t>::max() - 1);

    const size_t payload_bytes = MultiplyChecked(payload_stride_, RingCapacity());
    payloads_ = static_cast<std::byte*>(::operator new[](
        payload_bytes, std::align_val_t(PAYLOAD_ALLOC_ALIGN), std::nothrow));
    REQUIRE(payloads_ != nullptr);
  }

  /**
   * @brief 析构 SPSC 字节队列内核 / Destroy the SPSC byte-queue core
   */
  ~SPSCQueueBase()
  {
    ::operator delete[](payloads_, std::align_val_t(PAYLOAD_ALLOC_ALIGN));
  }

  /**
   * @brief 按字节入队一个 payload / Enqueue one payload by bytes
   * @param value 指向待入队 payload 的指针 / Pointer to the payload to enqueue
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`；空指针返回
   *         `ErrorCode::PTR_NULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full; returns `ErrorCode::PTR_NULL` when `value` is null
   */
  ErrorCode PushBytes(const void* value)
  {
    if (value == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }

    const auto current_tail = tail_.load(std::memory_order_relaxed);
    const auto next_tail = Increment(current_tail);

    if (next_tail == head_.load(std::memory_order_acquire))
    {
      return ErrorCode::FULL;
    }

    LibXR::Memory::FastCopy(PayloadPtr(current_tail), value, element_size_);
    tail_.store(next_tail, std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 按字节出队一个 payload；传空指针时仅丢弃队头元素
   *        / Dequeue one payload by bytes; pass null to discard the front item only
   * @param value 用于接收 payload 的缓冲区；传 `nullptr` 时仅丢弃
   *        / Buffer that receives the payload; pass `nullptr` to discard only
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode PopBytes(void* value = nullptr)
  {
    const auto current_head = head_.load(std::memory_order_relaxed);

    if (current_head == tail_.load(std::memory_order_acquire))
    {
      return ErrorCode::EMPTY;
    }

    if (value != nullptr)
    {
      LibXR::Memory::FastCopy(value, PayloadPtr(current_head), element_size_);
    }

    head_.store(Increment(current_head), std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 按字节查看一个队头 payload 但不出队 / Peek one front payload by bytes without dequeuing it
   * @param value 用于接收 payload 的缓冲区 / Buffer that receives the payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`；空指针返回
   *         `ErrorCode::PTR_NULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty; returns `ErrorCode::PTR_NULL` when `value` is null
   */
  ErrorCode PeekBytes(void* value)
  {
    if (value == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }

    const auto current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire))
    {
      return ErrorCode::EMPTY;
    }

    LibXR::Memory::FastCopy(value, PayloadPtr(current_head), element_size_);
    return ErrorCode::OK;
  }

  /**
   * @brief 按字节批量入队多个 payload / Enqueue multiple payloads by bytes
   * @param data 指向 payload 数组的字节指针 / Byte pointer to the payload array
   * @param count payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  ErrorCode PushBatchBytes(const void* data, size_t count)
  {
    if (count == 0U)
    {
      return ErrorCode::OK;
    }
    if (data == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }

    const auto current_tail = tail_.load(std::memory_order_relaxed);
    const auto current_head = head_.load(std::memory_order_acquire);
    const size_t capacity = RingCapacity();
    const size_t free_space =
        (current_tail >= current_head) ? (capacity - (current_tail - current_head) - 1)
                                       : (current_head - current_tail - 1);

    if (free_space < count)
    {
      return ErrorCode::FULL;
    }

    const auto* src = static_cast<const std::byte*>(data);
    for (size_t index = 0; index < count; ++index)
    {
      LibXR::Memory::FastCopy(PayloadPtr((current_tail + index) % capacity),
                              src + index * element_size_, element_size_);
    }

    tail_.store((current_tail + count) % capacity, std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 按字节批量出队多个 payload / Dequeue multiple payloads by bytes
   * @param data 用于接收 payload 的字节缓冲区；传 `nullptr` 时仅丢弃
   *        / Byte buffer receiving payloads; pass `nullptr` to discard only
   * @param count payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；元素不足返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         there are not enough payloads available
   */
  ErrorCode PopBatchBytes(void* data, size_t count)
  {
    if (count == 0U)
    {
      return ErrorCode::OK;
    }

    const auto current_head = head_.load(std::memory_order_relaxed);
    const auto current_tail = tail_.load(std::memory_order_acquire);
    const size_t capacity = RingCapacity();
    const size_t available = (current_tail >= current_head)
                                 ? (current_tail - current_head)
                                 : (capacity - current_head + current_tail);

    if (available < count)
    {
      return ErrorCode::EMPTY;
    }

    auto* dst = static_cast<std::byte*>(data);
    if (dst != nullptr)
    {
      for (size_t index = 0; index < count; ++index)
      {
        LibXR::Memory::FastCopy(dst + index * element_size_,
                                PayloadPtr((current_head + index) % capacity),
                                element_size_);
      }
    }

    head_.store((current_head + count) % capacity, std::memory_order_release);
    return ErrorCode::OK;
  }

  /**
   * @brief 按字节批量查看多个 payload 但不出队
   *        / Peek multiple payloads by bytes without dequeuing them
   * @param data 用于接收 payload 的字节缓冲区 / Byte buffer receiving payloads
   * @param count payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；元素不足返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         there are not enough payloads available
   */
  ErrorCode PeekBatchBytes(void* data, size_t count)
  {
    if (count == 0U)
    {
      return ErrorCode::OK;
    }
    if (data == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }

    const auto current_head = head_.load(std::memory_order_relaxed);
    const auto current_tail = tail_.load(std::memory_order_acquire);
    const size_t capacity = RingCapacity();
    const size_t available = (current_tail >= current_head)
                                 ? (current_tail - current_head)
                                 : (capacity - current_head + current_tail);

    if (available < count)
    {
      return ErrorCode::EMPTY;
    }

    auto* dst = static_cast<std::byte*>(data);
    for (size_t index = 0; index < count; ++index)
    {
      LibXR::Memory::FastCopy(dst + index * element_size_,
                              PayloadPtr((current_head + index) % capacity),
                              element_size_);
    }
    return ErrorCode::OK;
  }

  /**
   * @brief 重置队列状态 / Reset the queue state
   */
  void Reset()
  {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  /**
   * @brief 获取当前已用元素数 / Get the current element count
   * @return 当前元素个数 / Current number of stored payloads
   */
  size_t Size() const
  {
    const auto current_head = head_.load(std::memory_order_acquire);
    const auto current_tail = tail_.load(std::memory_order_acquire);
    return (current_tail >= current_head) ? (current_tail - current_head)
                                          : (RingCapacity() - current_head + current_tail);
  }

  /**
   * @brief 获取剩余空槽数 / Get the current free-slot count
   * @return 当前空槽个数 / Current number of free slots
   */
  size_t EmptySize() const { return capacity_ - Size(); }

  /**
   * @brief 获取队列最大容量 / Get the maximum queue capacity
   * @return 队列容量 / Queue capacity
   */
  size_t MaxSize() const { return capacity_; }

 private:
  /**
   * @brief 获取指定槽位 payload 起始地址 / Get the payload base address of one slot
   * @param index 目标槽位下标 / Target slot index
   * @return 指向目标槽位 payload 起始地址的指针 / Pointer to the slot payload base address
   */
  std::byte* PayloadPtr(IndexType index)
  {
    return payloads_ + index * payload_stride_;
  }

  /**
   * @brief 获取指定槽位 payload 起始地址（只读）
   *        / Get the payload base address of one slot (const)
   * @param index 目标槽位下标 / Target slot index
   * @return 指向目标槽位 payload 起始地址的只读指针
   *         / Read-only pointer to the slot payload base address
   */
  const std::byte* PayloadPtr(IndexType index) const
  {
    return payloads_ + index * payload_stride_;
  }

  /**
   * @brief 获取环形缓冲区的物理槽位总数 / Get the physical ring-slot count
   * @return 物理环形槽位总数（含一个保留空槽）
   *         / Physical ring-slot count including one reserved empty slot
   */
  size_t RingCapacity() const { return capacity_ + 1; }

  /**
   * @brief 沿环形缓冲区推进一个槽位 / Advance one slot along the ring
   * @param index 当前槽位下标 / Current slot index
   * @return 推进后的槽位下标 / Advanced slot index
   */
  IndexType Increment(IndexType index) const
  {
    return (index + 1) % RingCapacity();
  }

  /**
   * @brief payload 缓冲区整体分配对齐 / Allocation alignment used for the whole payload buffer
   */
  static constexpr size_t PAYLOAD_ALLOC_ALIGN =
      std::max(alignof(size_t), alignof(std::max_align_t));

  /// @brief 禁止拷贝构造 / Non-copyable.
  SPSCQueueBase(const SPSCQueueBase&);
  /// @brief 禁止拷贝赋值 / Non-copy-assignable.
  SPSCQueueBase& operator=(const SPSCQueueBase&);
  /// @brief 禁止移动构造 / Non-movable.
  SPSCQueueBase(SPSCQueueBase&&);
  /// @brief 禁止移动赋值 / Non-move-assignable.
  SPSCQueueBase& operator=(SPSCQueueBase&&);

  /**
   * @brief 安全地向上对齐字节数 / Safely align one byte count upward
   * @param size 待对齐字节数 / Byte count to align
   * @param align 目标对齐粒度 / Target alignment granularity
   * @return 对齐后的字节数 / Aligned byte count
   */
  static size_t AlignUpChecked(size_t size, size_t align)
  {
    REQUIRE(align > 0);
    REQUIRE(size <= std::numeric_limits<size_t>::max() - (align - 1));
    return ((size + align - 1) / align) * align;
  }

  /**
   * @brief 安全地计算两个字节数的乘积 / Safely multiply two byte counts
   * @param lhs 左操作数 / Left operand
   * @param rhs 右操作数 / Right operand
   * @return 乘积结果 / Product result
   */
  static size_t MultiplyChecked(size_t lhs, size_t rhs)
  {
    if (lhs == 0 || rhs == 0)
    {
      return 0;
    }

    REQUIRE(lhs <= std::numeric_limits<size_t>::max() / rhs);
    return lhs * rhs;
  }

  const size_t element_size_;    ///< 单个 payload 的字节数 / Byte size of one payload.
  const size_t capacity_;        ///< 队列容量 / Queue capacity.
  const size_t payload_stride_;  ///< 相邻 payload 槽位之间的步长 / Byte stride between adjacent payload slots.
  std::byte* payloads_;          ///< payload 字节缓冲区 / Byte buffer storing payloads.

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<IndexType>
      head_;  ///< 下一个待出队的环形下标 / Next ring index to dequeue.
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<IndexType>
      tail_;  ///< 下一个待入队的环形下标 / Next ring index to enqueue.
};
}  // namespace LibXR
