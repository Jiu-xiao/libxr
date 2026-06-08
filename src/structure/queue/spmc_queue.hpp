#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

#include "libxr_def.hpp"
#include "queue_typed_base.hpp"

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

  /**
   * @brief 构造 SPMC 字节队列 / Construct an SPMC byte queue
   * @param element_size 单个 payload 的字节数 / Byte size of one payload
   * @param capacity 队列容量 / Queue capacity
   */
  SPMCQueueBase(size_t element_size, size_t capacity);

  /**
   * @brief 析构 SPMC 字节队列 / Destroy the SPMC byte queue
   */
  ~SPMCQueueBase();

  /**
   * @brief 按字节入队一个 payload / Enqueue one payload by bytes
   * @param value 指向待入队 payload 的指针 / Pointer to the payload to enqueue
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`；空指针返回
   *         `ErrorCode::PTR_NULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full; returns `ErrorCode::PTR_NULL` when `value` is null
   */
  ErrorCode PushBytes(const void* value);

  /**
   * @brief 按字节出队一个 payload；传空指针时只丢弃队头元素
   *        / Dequeue one payload by bytes; pass null to discard the front item
   * @param value 用于接收 payload 的缓冲区；传 `nullptr` 时仅丢弃
   *        / Buffer receiving the payload; pass `nullptr` to discard only
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode PopBytes(void* value = nullptr);

  /**
   * @brief 按字节批量入队多个 payload / Enqueue multiple payloads by bytes
   * @param data 指向 payload 数组的字节指针 / Byte pointer to the payload array
   * @param count payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空间不足返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         there are not enough free slots
   */
  ErrorCode PushBatchBytes(const void* data, size_t count);

  /**
   * @brief 按字节批量出队多个 payload / Dequeue multiple payloads by bytes
   * @param data 用于接收 payload 的字节缓冲区；传 `nullptr` 时仅丢弃
   *        / Byte buffer receiving payloads; pass `nullptr` to discard only
   * @param count payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；元素不足返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         there are not enough payloads available
   */
  ErrorCode PopBatchBytes(void* data, size_t count);

  /**
   * @brief 重置队列状态 / Reset the queue state
   *
   * @note 只能在生产者和所有消费者都停止访问时调用。
   *       Call only after the producer and all consumers have stopped accessing
   *       the queue.
   */
  void Reset();

  /**
   * @brief 获取并发快照下的当前元素数 / Get the current approximate element count
   * @return 当前元素数，最大钳到 `MaxSize()` / Current count clamped to `MaxSize()`
   */
  [[nodiscard]] size_t Size() const;

  /**
   * @brief 获取剩余空槽数 / Get the current free-slot count
   * @return 当前空槽个数 / Current number of free slots
   */
  [[nodiscard]] size_t EmptySize() const { return capacity_ - Size(); }

  /**
   * @brief 获取队列最大容量 / Get the maximum queue capacity
   * @return 队列容量 / Queue capacity
   */
  [[nodiscard]] size_t MaxSize() const { return capacity_; }

  /**
   * @brief 获取单个 payload 的字节数 / Get the byte size of one payload
   * @return 单个 payload 的字节数 / Byte size of one payload
   */
  [[nodiscard]] size_t ElementSize() const { return element_size_; }

 private:
  /// @brief 每个逻辑槽对应的序号单元 / Sequence cell for one logical slot.
  struct alignas(LibXR::CONCURRENCY_ALIGNMENT) SequenceCell
  {
    std::atomic<SequenceType> value;  ///< 当前槽的逻辑序号 / Current slot sequence.
  };

  /// @brief 获取指定槽位 payload 起始地址 / Get one slot payload address.
  [[nodiscard]] void* PayloadPtr(size_t index);
  /// @brief 获取指定槽位 payload 起始地址（只读） / Get one slot payload address (const).
  [[nodiscard]] const void* PayloadPtr(size_t index) const;
  /// @brief 安全地向上对齐字节数 / Safely align one byte count upward.
  [[nodiscard]] static size_t AlignUpChecked(size_t value, size_t align);
  /// @brief 安全地计算乘积 / Safely multiply two size values.
  [[nodiscard]] static size_t MultiplyChecked(size_t lhs, size_t rhs);
  static constexpr size_t PAYLOAD_ALLOC_ALIGN =
      std::max(alignof(size_t),
               alignof(std::max_align_t));  ///< payload 缓冲区整体分配对齐 / Payload buffer allocation alignment.

  /// @brief 禁止拷贝构造 / Non-copyable.
  SPMCQueueBase(const SPMCQueueBase&);
  /// @brief 禁止拷贝赋值 / Non-copy-assignable.
  SPMCQueueBase& operator=(const SPMCQueueBase&);
  /// @brief 禁止移动构造 / Non-movable.
  SPMCQueueBase(SPMCQueueBase&&);
  /// @brief 禁止移动赋值 / Non-move-assignable.
  SPMCQueueBase& operator=(SPMCQueueBase&&);

  const size_t element_size_;    ///< 单个 payload 的字节数 / Byte size of one payload.
  const size_t capacity_;        ///< 队列容量 / Queue capacity.
  const size_t payload_stride_;  ///< 相邻 payload 槽位之间的步长 / Byte stride between adjacent payload slots.
  SequenceCell* sequences_;      ///< 槽序号数组 / Array of per-slot sequence cells.
  std::byte* payloads_;          ///< payload 字节缓冲区 / Byte buffer storing payloads.

  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType>
      head_;  ///< 下一个待出队的逻辑位置 / Next logical dequeue position.
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<SequenceType>
      tail_;  ///< 下一个待入队的逻辑位置 / Next logical enqueue position.
};

/**
 * @class SPMCQueue
 * @brief 带固定 payload 类型的 SPMC 队列 / SPMC queue with a fixed payload type
 *
 * 模板壳只把 `Data` 映射为固定大小的字节 payload，不在队列内部管理 `Data`
 * 对象生命周期。多消费者只通过 byte base 的 claim/release 协议同步槽位。
 *
 * This wrapper maps `Data` to fixed-size byte payloads and does not manage
 * `Data` object lifetime inside the queue. Multiple consumers synchronize slot
 * ownership only through the byte base claim/release protocol.
 *
 * @tparam Data 队列存储的数据类型 / Queue element type.
 */
template <typename Data>
class SPMCQueue final : public QueueTypedBase<SPMCQueue<Data>, Data>,
                        public SPMCQueueBase
{
 public:
  using ValueType = Data;  ///< 队列元素类型 / Queue element type.
  using QueueTypedBase<SPMCQueue<Data>, Data>::Pop;
  using QueueTypedBase<SPMCQueue<Data>, Data>::Push;

  /**
   * @brief 构造一个 SPMC 队列 / Construct one SPMC queue
   * @param length 队列容量 / Queue capacity
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  explicit SPMCQueue(size_t length) : SPMCQueueBase(sizeof(Data), length) {}

  /**
   * @brief 批量推入多个 payload / Push multiple payloads into the queue
   * @param data payload 数组指针 / Pointer to the payload array
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空间不足返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         there are not enough free slots
   */
  ErrorCode PushBatch(const Data* data, size_t size)
  {
    return SPMCQueueBase::PushBatchBytes(data, size);
  }

  /**
   * @brief 批量弹出多个 payload / Pop multiple payloads from the queue
   * @param data 用于接收 payload 的数组；传 `nullptr` 时仅丢弃
   *        / Array receiving payloads; pass `nullptr` to discard only
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；元素不足返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         there are not enough payloads available
   */
  ErrorCode PopBatch(Data* data, size_t size)
  {
    return SPMCQueueBase::PopBatchBytes(data, size);
  }

  /**
   * @brief 重置队列状态 / Reset the queue state
   */
  void Reset() { SPMCQueueBase::Reset(); }
};
}  // namespace LibXR
