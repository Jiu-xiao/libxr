#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "libxr_mem.hpp"

namespace LibXR::Detail
{
/**
 * @class SPQueueCoreImpl
 * @brief 单生产者队列共享内核实现 / Shared single-producer queue core implementation
 *
 * 这个 detail 级实现只负责单生产者 ring 的公共索引推进、容量判断与原始字节
 * 存储。`SPSCQueueCore` 与 `SPMCQueueCore` 通过 `MultiConsumer` 模板参数共享它。
 *
 * This detail-level implementation contains only the common ring progression,
 * capacity checks, and raw byte storage for single-producer queues.
 * `SPSCQueueCore` and `SPMCQueueCore` share it through the `MultiConsumer`
 * template parameter.
 */
template <bool MultiConsumer>
class alignas(LibXR::CONCURRENCY_ALIGNMENT) SPQueueCoreImpl
{
 public:
  using IndexType = size_t;

  /**
   * @brief 构造单生产者队列共享内核 / Construct the shared single-producer queue core
   * @param element_size 单个 payload 的字节数 / Byte size of one payload
   * @param capacity 队列容量 / Queue capacity
   */
  SPQueueCoreImpl(size_t element_size, size_t capacity)
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
   * @brief 析构单生产者队列共享内核 / Destroy the shared single-producer queue core
   */
  ~SPQueueCoreImpl()
  {
    ::operator delete[](payloads_, std::align_val_t(PAYLOAD_ALLOC_ALIGN));
  }

  /// @brief 禁止拷贝构造 / Non-copyable.
  SPQueueCoreImpl(const SPQueueCoreImpl&) = delete;
  /// @brief 禁止拷贝赋值 / Non-copy-assignable.
  SPQueueCoreImpl& operator=(const SPQueueCoreImpl&) = delete;
  /// @brief 禁止移动构造 / Non-movable.
  SPQueueCoreImpl(SPQueueCoreImpl&&) = delete;
  /// @brief 禁止移动赋值 / Non-move-assignable.
  SPQueueCoreImpl& operator=(SPQueueCoreImpl&&) = delete;

  /**
   * @brief 获取指定槽位 payload 起始地址 / Get the payload base address of one slot
   * @param index 目标槽位下标 / Target slot index
   */
  std::byte* PayloadPtr(IndexType index)
  {
    return payloads_ + index * payload_stride_;
  }

  /**
   * @brief 获取指定槽位 payload 起始地址（只读） / Get the payload base address of one slot (const)
   * @param index 目标槽位下标 / Target slot index
   */
  const std::byte* PayloadPtr(IndexType index) const
  {
    return payloads_ + index * payload_stride_;
  }

  template <typename Writer>
  ErrorCode PushWithSlot(Writer&& writer)
  {
    static_assert(std::is_invocable_v<Writer&, std::byte*>,
                  "PushWithSlot writer must be callable as ErrorCode(std::byte*)");
    using WriterRet = std::invoke_result_t<Writer&, std::byte*>;
    static_assert(std::is_convertible_v<WriterRet, ErrorCode>,
                  "PushWithSlot writer return type must be convertible to ErrorCode");

    const auto current_tail = tail_.load(std::memory_order_relaxed);
    const auto next_tail = Increment(current_tail);

    if (next_tail == head_.load(std::memory_order_acquire))
    {
      return ErrorCode::FULL;
    }

    Writer& writer_ref = writer;
    const ErrorCode ec = writer_ref(PayloadPtr(current_tail));
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    tail_.store(next_tail, std::memory_order_release);
    return ErrorCode::OK;
  }

  template <typename Reader>
  ErrorCode PopWithSlot(Reader&& reader)
  {
    static_assert(std::is_invocable_v<Reader&, const std::byte*>,
                  "PopWithSlot reader must be callable as ErrorCode(const std::byte*)");
    using ReaderRet = std::invoke_result_t<Reader&, const std::byte*>;
    static_assert(std::is_convertible_v<ReaderRet, ErrorCode>,
                  "PopWithSlot reader return type must be convertible to ErrorCode");

    auto current_head = head_.load(std::memory_order_relaxed);

    while (true)
    {
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      Reader& reader_ref = reader;
      const ErrorCode ec = reader_ref(PayloadPtr(current_head));
      if (ec != ErrorCode::OK)
      {
        return ec;
      }

      if constexpr (MultiConsumer)
      {
        if (head_.compare_exchange_weak(current_head, Increment(current_head),
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed))
        {
          return ErrorCode::OK;
        }
      }
      else
      {
        head_.store(Increment(current_head), std::memory_order_release);
        return ErrorCode::OK;
      }
    }
  }

  template <typename Reader>
  ErrorCode PeekWithSlot(Reader&& reader)
  {
    static_assert(std::is_invocable_v<Reader&, const std::byte*>,
                  "PeekWithSlot reader must be callable as ErrorCode(const std::byte*)");
    using ReaderRet = std::invoke_result_t<Reader&, const std::byte*>;
    static_assert(std::is_convertible_v<ReaderRet, ErrorCode>,
                  "PeekWithSlot reader return type must be convertible to ErrorCode");

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      if (current_head == tail_.load(std::memory_order_acquire))
      {
        return ErrorCode::EMPTY;
      }

      Reader& reader_ref = reader;
      const ErrorCode ec = reader_ref(PayloadPtr(current_head));
      if (ec != ErrorCode::OK)
      {
        return ec;
      }

      if constexpr (MultiConsumer)
      {
        if (head_.load(std::memory_order_acquire) == current_head)
        {
          return ErrorCode::OK;
        }
      }
      else
      {
        return ErrorCode::OK;
      }
    }
  }

  template <typename Writer>
  ErrorCode PushWithSpan(size_t count, Writer&& writer)
  {
    static_assert(std::is_invocable_v<Writer&, std::byte*, size_t>,
                  "PushWithSpan writer must be callable as "
                  "ErrorCode(std::byte* buffer, size_t chunk_count)");
    using WriterRet = std::invoke_result_t<Writer&, std::byte*, size_t>;
    static_assert(std::is_convertible_v<WriterRet, ErrorCode>,
                  "PushWithSpan writer return type must be convertible to ErrorCode");

    if (count == 0U)
    {
      return ErrorCode::OK;
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

    const size_t first_chunk = LibXR::min(count, capacity - static_cast<size_t>(current_tail));
    Writer& writer_ref = writer;
    const ErrorCode first_ec = writer_ref(PayloadPtr(current_tail), first_chunk);
    if (first_ec != ErrorCode::OK)
    {
      return first_ec;
    }

    if (count > first_chunk)
    {
      const ErrorCode second_ec = writer_ref(PayloadPtr(0), count - first_chunk);
      if (second_ec != ErrorCode::OK)
      {
        return second_ec;
      }
    }

    tail_.store((current_tail + count) % capacity, std::memory_order_release);
    return ErrorCode::OK;
  }

  template <typename Reader>
  ErrorCode PopWithSpan(size_t count, Reader&& reader)
  {
    static_assert(std::is_invocable_v<Reader&, const std::byte*, size_t>,
                  "PopWithSpan reader must be callable as "
                  "ErrorCode(const std::byte* buffer, size_t chunk_count)");
    using ReaderRet = std::invoke_result_t<Reader&, const std::byte*, size_t>;
    static_assert(std::is_convertible_v<ReaderRet, ErrorCode>,
                  "PopWithSpan reader return type must be convertible to ErrorCode");

    if (count == 0U)
    {
      return ErrorCode::OK;
    }

    const size_t capacity = RingCapacity();

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      auto current_tail = tail_.load(std::memory_order_acquire);
      const size_t available = (current_tail >= current_head)
                                   ? (current_tail - current_head)
                                   : (capacity - current_head + current_tail);

      if (available < count)
      {
        return ErrorCode::EMPTY;
      }

      Reader& reader_ref = reader;
      const size_t first_chunk =
          LibXR::min(count, capacity - static_cast<size_t>(current_head));
      const ErrorCode first_ec = reader_ref(PayloadPtr(current_head), first_chunk);
      if (first_ec != ErrorCode::OK)
      {
        return first_ec;
      }

      if (count > first_chunk)
      {
        const ErrorCode second_ec = reader_ref(PayloadPtr(0), count - first_chunk);
        if (second_ec != ErrorCode::OK)
        {
          return second_ec;
        }
      }

      const IndexType new_head = (current_head + count) % capacity;
      if constexpr (MultiConsumer)
      {
        if (head_.compare_exchange_weak(current_head, new_head, std::memory_order_acq_rel,
                                        std::memory_order_relaxed))
        {
          return ErrorCode::OK;
        }
      }
      else
      {
        head_.store(new_head, std::memory_order_release);
        return ErrorCode::OK;
      }
    }
  }

  template <typename Reader>
  ErrorCode PeekWithSpan(size_t count, Reader&& reader)
  {
    static_assert(std::is_invocable_v<Reader&, const std::byte*, size_t>,
                  "PeekWithSpan reader must be callable as "
                  "ErrorCode(const std::byte* buffer, size_t chunk_count)");
    using ReaderRet = std::invoke_result_t<Reader&, const std::byte*, size_t>;
    static_assert(std::is_convertible_v<ReaderRet, ErrorCode>,
                  "PeekWithSpan reader return type must be convertible to ErrorCode");

    if (count == 0U)
    {
      return ErrorCode::OK;
    }

    const size_t capacity = RingCapacity();

    while (true)
    {
      auto current_head = head_.load(std::memory_order_relaxed);
      auto current_tail = tail_.load(std::memory_order_acquire);
      const size_t available = (current_tail >= current_head)
                                   ? (current_tail - current_head)
                                   : (capacity - current_head + current_tail);

      if (available < count)
      {
        return ErrorCode::EMPTY;
      }

      Reader& reader_ref = reader;
      const size_t first_chunk =
          LibXR::min(count, capacity - static_cast<size_t>(current_head));
      const ErrorCode first_ec = reader_ref(PayloadPtr(current_head), first_chunk);
      if (first_ec != ErrorCode::OK)
      {
        return first_ec;
      }

      if (count > first_chunk)
      {
        const ErrorCode second_ec = reader_ref(PayloadPtr(0), count - first_chunk);
        if (second_ec != ErrorCode::OK)
        {
          return second_ec;
        }
      }

      if constexpr (MultiConsumer)
      {
        if (head_.load(std::memory_order_acquire) == current_head)
        {
          return ErrorCode::OK;
        }
      }
      else
      {
        return ErrorCode::OK;
      }
    }
  }

  ErrorCode PushBytes(const void* value)
  {
    if (value == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }
    return PushWithSlot(
        [&](std::byte* slot)
        {
          LibXR::Memory::FastCopy(slot, value, element_size_);
          return ErrorCode::OK;
        });
  }

  ErrorCode PopBytes(void* value = nullptr)
  {
    return PopWithSlot(
        [&](const std::byte* slot)
        {
          if (value != nullptr)
          {
            LibXR::Memory::FastCopy(value, slot, element_size_);
          }
          return ErrorCode::OK;
        });
  }

  ErrorCode PeekBytes(void* value)
  {
    if (value == nullptr)
    {
      return ErrorCode::PTR_NULL;
    }
    return PeekWithSlot(
        [&](const std::byte* slot)
        {
          LibXR::Memory::FastCopy(value, slot, element_size_);
          return ErrorCode::OK;
        });
  }

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

    const auto* src = static_cast<const std::byte*>(data);
    size_t offset = 0;
    return PushWithSpan(
        count,
        [&](std::byte* buffer, size_t chunk_count)
        {
          LibXR::Memory::FastCopy(buffer, src + offset * element_size_,
                                  chunk_count * element_size_);
          offset += chunk_count;
          return ErrorCode::OK;
        });
  }

  ErrorCode PopBatchBytes(void* data, size_t count)
  {
    if (count == 0U)
    {
      return ErrorCode::OK;
    }

    auto* dst = static_cast<std::byte*>(data);
    size_t offset = 0;
    return PopWithSpan(
        count,
        [&](const std::byte* buffer, size_t chunk_count)
        {
          if (dst != nullptr)
          {
            LibXR::Memory::FastCopy(dst + offset * element_size_, buffer,
                                    chunk_count * element_size_);
          }
          offset += chunk_count;
          return ErrorCode::OK;
        });
  }

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

    auto* dst = static_cast<std::byte*>(data);
    size_t offset = 0;
    return PeekWithSpan(
        count,
        [&](const std::byte* buffer, size_t chunk_count)
        {
          LibXR::Memory::FastCopy(dst + offset * element_size_, buffer,
                                  chunk_count * element_size_);
          offset += chunk_count;
          return ErrorCode::OK;
        });
  }

  void Reset()
  {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  size_t Size() const
  {
    const auto current_head = head_.load(std::memory_order_acquire);
    const auto current_tail = tail_.load(std::memory_order_acquire);
    return (current_tail >= current_head) ? (current_tail - current_head)
                                          : (RingCapacity() - current_head + current_tail);
  }

  size_t EmptySize() const { return capacity_ - Size(); }
  size_t MaxSize() const { return capacity_; }

  size_t RingCapacity() const { return capacity_ + 1; }
  IndexType LoadHeadRelaxed() const { return head_.load(std::memory_order_relaxed); }
  IndexType LoadHeadAcquire() const { return head_.load(std::memory_order_acquire); }
  IndexType LoadTailRelaxed() const { return tail_.load(std::memory_order_relaxed); }
  IndexType LoadTailAcquire() const { return tail_.load(std::memory_order_acquire); }
  bool CompareExchangeHead(IndexType& expected, IndexType desired)
  {
    return head_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel,
                                       std::memory_order_relaxed);
  }
  void StoreHeadRelease(IndexType value)
  {
    head_.store(value, std::memory_order_release);
  }
  void StoreTailRelease(IndexType value)
  {
    tail_.store(value, std::memory_order_release);
  }

 protected:
  IndexType Increment(IndexType index) const
  {
    return (index + 1) % RingCapacity();
  }

  const size_t element_size_;    ///< 单个 payload 的字节数 / Byte size of one payload.
  const size_t capacity_;        ///< 队列容量 / Queue capacity.
  const size_t payload_stride_;  ///< 相邻 payload 槽位之间的步长 / Byte stride between adjacent payload slots.
  std::byte* payloads_;          ///< payload 字节缓冲区 / Byte buffer storing payloads.

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<IndexType>
      head_;  ///< 下一个待出队的环形下标 / Next ring index to dequeue.
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<IndexType>
      tail_;  ///< 下一个待入队的环形下标 / Next ring index to enqueue.

 private:
  static constexpr size_t PAYLOAD_ALLOC_ALIGN =
      std::max(alignof(size_t), alignof(std::max_align_t));

  static size_t AlignUpChecked(size_t size, size_t align)
  {
    REQUIRE(align > 0);
    REQUIRE(size <= std::numeric_limits<size_t>::max() - (align - 1));
    return ((size + align - 1) / align) * align;
  }

  static size_t MultiplyChecked(size_t lhs, size_t rhs)
  {
    if (lhs == 0 || rhs == 0)
    {
      return 0;
    }

    REQUIRE(lhs <= std::numeric_limits<size_t>::max() / rhs);
    return lhs * rhs;
  }
};

}  // namespace LibXR::Detail
