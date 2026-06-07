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
  using IndexType = size_t;  ///< 环形缓冲区索引类型 / Ring-buffer index type.

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

  /**
   * @brief 通过单槽写入器推进一次入队 / Enqueue one payload via a single-slot writer
   * @tparam Writer 写入器类型 / Writer callback type
   * @param writer 写入器回调，签名为 `ErrorCode(std::byte* slot)`
   *        / Writer callback with signature `ErrorCode(std::byte* slot)`
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
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

  /**
   * @brief 通过单槽读取器推进一次出队 / Dequeue one payload via a single-slot reader
   * @tparam Reader 读取器类型 / Reader callback type
   * @param reader 读取器回调，签名为 `ErrorCode(const std::byte* slot)`
   *        / Reader callback with signature `ErrorCode(const std::byte* slot)`
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
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

  /**
   * @brief 通过单槽读取器查看队头但不出队 / Peek the front payload via a single-slot reader
   * @tparam Reader 读取器类型 / Reader callback type
   * @param reader 读取器回调，签名为 `ErrorCode(const std::byte* slot)`
   *        / Reader callback with signature `ErrorCode(const std::byte* slot)`
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
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

  /**
   * @brief 通过跨度写入器批量入队 / Enqueue multiple payloads via a span writer
   * @tparam Writer 写入器类型 / Writer callback type
   * @param count 需要写入的 payload 个数 / Number of payloads to write
   * @param writer 写入器回调，签名为
   *        `ErrorCode(std::byte* buffer, size_t chunk_count)`
   *        / Writer callback with signature
   *        `ErrorCode(std::byte* buffer, size_t chunk_count)`
   * @return 返回写入结果；空间不足返回 `ErrorCode::FULL`
   *         Returns the writer result; returns `ErrorCode::FULL` when there is
   *         not enough free space
   *
   * @note 这里的 `chunk_count` 表示连续槽位数量，不是字节数。
   *       Here `chunk_count` is the number of contiguous slots, not the number
   *       of bytes.
   */
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

  /**
   * @brief 通过跨度读取器批量出队 / Dequeue multiple payloads via a span reader
   * @tparam Reader 读取器类型 / Reader callback type
   * @param count 需要读取的 payload 个数 / Number of payloads to read
   * @param reader 读取器回调，签名为
   *        `ErrorCode(const std::byte* buffer, size_t chunk_count)`
   *        / Reader callback with signature
   *        `ErrorCode(const std::byte* buffer, size_t chunk_count)`
   * @return 返回读取结果；元素不足返回 `ErrorCode::EMPTY`
   *         Returns the reader result; returns `ErrorCode::EMPTY` when there
   *         are not enough payloads available
   *
   * @note 这里的 `chunk_count` 表示连续槽位数量，不是字节数。
   *       Here `chunk_count` is the number of contiguous slots, not the number
   *       of bytes.
   */
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

  /**
   * @brief 通过跨度读取器批量查看但不出队 / Peek multiple payloads via a span reader without dequeuing them
   * @tparam Reader 读取器类型 / Reader callback type
   * @param count 需要查看的 payload 个数 / Number of payloads to peek
   * @param reader 读取器回调，签名为
   *        `ErrorCode(const std::byte* buffer, size_t chunk_count)`
   *        / Reader callback with signature
   *        `ErrorCode(const std::byte* buffer, size_t chunk_count)`
   * @return 返回读取结果；元素不足返回 `ErrorCode::EMPTY`
   *         Returns the reader result; returns `ErrorCode::EMPTY` when there
   *         are not enough payloads available
   *
   * @note 这里的 `chunk_count` 表示连续槽位数量，不是字节数。
   *       Here `chunk_count` is the number of contiguous slots, not the number
   *       of bytes.
   */
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
    return PushWithSlot(
        [&](std::byte* slot)
        {
          LibXR::Memory::FastCopy(slot, value, element_size_);
          return ErrorCode::OK;
        });
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
    return PeekWithSlot(
        [&](const std::byte* slot)
        {
          LibXR::Memory::FastCopy(value, slot, element_size_);
          return ErrorCode::OK;
        });
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

  /**
   * @brief 获取环形缓冲区的物理槽位总数 / Get the physical ring-slot count
   * @return 物理环形槽位总数（含一个保留空槽） / Physical ring-slot count including one reserved empty slot
   */
  size_t RingCapacity() const { return capacity_ + 1; }
  /**
   * @brief 以 relaxed 语义读取 head / Load head with relaxed ordering
   */
  IndexType LoadHeadRelaxed() const { return head_.load(std::memory_order_relaxed); }
  /**
   * @brief 以 acquire 语义读取 head / Load head with acquire ordering
   */
  IndexType LoadHeadAcquire() const { return head_.load(std::memory_order_acquire); }
  /**
   * @brief 以 relaxed 语义读取 tail / Load tail with relaxed ordering
   */
  IndexType LoadTailRelaxed() const { return tail_.load(std::memory_order_relaxed); }
  /**
   * @brief 以 acquire 语义读取 tail / Load tail with acquire ordering
   */
  IndexType LoadTailAcquire() const { return tail_.load(std::memory_order_acquire); }
  /**
   * @brief 通过 CAS 推进 head / Advance head via compare-exchange
   * @param expected 期望的旧值 / Expected old value
   * @param desired 目标新值 / Desired new value
   * @return 成功推进返回 `true` / Returns `true` when the update succeeds
   */
  bool CompareExchangeHead(IndexType& expected, IndexType desired)
  {
    return head_.compare_exchange_weak(expected, desired, std::memory_order_acq_rel,
                                       std::memory_order_relaxed);
  }
  /**
   * @brief 以 release 语义写入 head / Store head with release ordering
   * @param value 新的 head 值 / New head value
   */
  void StoreHeadRelease(IndexType value)
  {
    head_.store(value, std::memory_order_release);
  }
  /**
   * @brief 以 release 语义写入 tail / Store tail with release ordering
   * @param value 新的 tail 值 / New tail value
   */
  void StoreTailRelease(IndexType value)
  {
    tail_.store(value, std::memory_order_release);
  }

 protected:
  /**
   * @brief 沿环形缓冲区推进一个槽位 / Advance one slot along the ring
   * @param index 当前槽位下标 / Current slot index
   * @return 推进后的槽位下标 / Advanced slot index
   */
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
  /**
   * @brief payload 缓冲区整体分配对齐 / Allocation alignment used for the whole payload buffer
   */
  static constexpr size_t PAYLOAD_ALLOC_ALIGN =
      std::max(alignof(size_t), alignof(std::max_align_t));

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
};

}  // namespace LibXR::Detail
