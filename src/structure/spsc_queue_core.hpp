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

namespace LibXR
{
/**
 * @class SPSCQueueCore
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
class alignas(LibXR::CONCURRENCY_ALIGNMENT) SPSCQueueCore
{
 public:
  using IndexType = size_t;  ///< 环形缓冲区索引类型 / Ring-buffer index type.

  /**
   * @brief 构造 SPSC 字节队列内核 / Construct the SPSC byte-queue core
   * @param element_size 单个 payload 的字节数 / Byte size of one payload
   * @param capacity 队列容量 / Queue capacity
   */
  SPSCQueueCore(size_t element_size, size_t capacity)
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
  ~SPSCQueueCore()
  {
    ::operator delete[](payloads_, std::align_val_t(PAYLOAD_ALLOC_ALIGN));
  }

  /// @brief 禁止拷贝构造 / Non-copyable.
  SPSCQueueCore(const SPSCQueueCore&) = delete;
  /// @brief 禁止拷贝赋值 / Non-copy-assignable.
  SPSCQueueCore& operator=(const SPSCQueueCore&) = delete;
  /// @brief 禁止移动构造 / Non-movable.
  SPSCQueueCore(SPSCQueueCore&&) = delete;
  /// @brief 禁止移动赋值 / Non-move-assignable.
  SPSCQueueCore& operator=(SPSCQueueCore&&) = delete;

  /**
   * @brief 获取指定槽位 payload 起始地址 / Get the payload base address of one slot
   * @param index 目标槽位下标 / Target slot index
   */
  std::byte* PayloadPtr(IndexType index)
  {
    return payloads_ + index * payload_stride_;
  }

  /**
   * @brief 获取指定槽位 payload 起始地址（只读）
   *        / Get the payload base address of one slot (const)
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

    const auto current_head = head_.load(std::memory_order_relaxed);

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

    head_.store(Increment(current_head), std::memory_order_release);
    return ErrorCode::OK;
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

    const auto current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire))
    {
      return ErrorCode::EMPTY;
    }

    Reader& reader_ref = reader;
    return reader_ref(PayloadPtr(current_head));
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
   * @return 物理环形槽位总数（含一个保留空槽）
   *         / Physical ring-slot count including one reserved empty slot
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
}  // namespace LibXR
