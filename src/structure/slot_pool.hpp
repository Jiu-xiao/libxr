#pragma once

/**
 * @file slot_pool.hpp
 * @brief 定长槽池：任务与中断共享的小容量消息槽 / Fixed-size slot pool for
 *        small task/interrupt shared message slots.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "libxr_def.hpp"
#include "libxr_mem.hpp"

namespace LibXR
{
/**
 * @class SlotPoolBase
 * @brief 槽池的无类型内核：槽状态机、发布序号与扫描逻辑。
 *        Typeless core of the slot pool: slot state machine, publish sequence
 *        and scan logic.
 *
 * 内核只管理每个槽的原子状态与发布序号，以及按槽位下标访问的原始存储；
 * payload 类型语义由上层的 `SlotPool<T>` 负责。所有状态转换都用 CAS 完成，
 * 同一时刻一个槽只属于一个执行者。
 *
 * The core only manages the per-slot atomic state, the publish sequence and the
 * raw per-index storage; payload type semantics live in `SlotPool<T>` above it.
 * Every state transition is a CAS, so at any moment a slot belongs to exactly
 * one execution context.
 */
class SlotPoolBase
{
 public:
  /**
   * @brief 构造槽池内核 / Construct the slot-pool core.
   * @param slot_size 单个槽的字节数 / Byte size of one slot
   * @param slot_align 槽存储的对齐要求 / Alignment requirement of the slot storage
   * @param slot_count 槽总数 / Total number of slots
   *
   * @note 仅构造时分配内存，运行期不再分配 / Allocates only at construction;
   *       no allocation at run time.
   */
  SlotPoolBase(size_t slot_size, size_t slot_align, size_t slot_count);
  /**
   * @brief 析构槽池内核 / Destroy the slot-pool core.
   */
  ~SlotPoolBase();

  /**
   * @brief 认领一个空闲槽（空闲 → 写入中） / Claim one free slot (free -> writing).
   * @param index 成功时写入认领的槽位下标 / Receives the claimed slot index on success
   * @return 成功返回 `ErrorCode::OK`；没有空闲槽返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; `ErrorCode::FULL` when no slot
   *         is free
   *
   * 扫描全部槽位，跳过被其他上下文占用的槽（写入中、就绪、读取中）。
   * 一个被打断的操作只占住它自己的槽，不会挡住其他上下文认领剩余的空闲槽。
   * The full slot array is scanned and slots owned by other contexts (writing,
   * ready, reading) are skipped. An interrupted operation only occupies its own
   * slot and never blocks other contexts from claiming the remaining free slots.
   */
  ErrorCode Acquire(size_t& index);
  /**
   * @brief 发布已写好的槽（写入中 → 就绪），并取得发布序号
   *        / Publish a fully written slot (writing -> ready) and take a publish
   *        sequence number.
   * @param index 已通过 `Acquire` 认领的槽位下标 / Slot index claimed via `Acquire`
   *
   * 发布序号由全局计数器 `fetch_add` 取得；`Take` 按发布完成的先后出队。
   * 调用方必须仍持有该槽（写入中），重复发布或发布未认领的槽是调用方错误。
   * The sequence number comes from a global `fetch_add` counter; `Take` dequeues
   * in publish-completion order. The caller must still own the slot (writing);
   * double publishing or publishing an unclaimed slot is a caller error.
   */
  void Publish(size_t index);
  /**
   * @brief 认领序号最小的就绪槽（就绪 → 读取中）
   *        / Claim the oldest ready slot (ready -> reading).
   * @param index 成功时写入认领的槽位下标 / Receives the claimed slot index on success
   * @return 成功返回 `ErrorCode::OK`；没有就绪槽返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; `ErrorCode::EMPTY` when no slot
   *         is ready
   *
   * 槽之间按发布序号挑选（序号比较允许回绕），因此出队顺序是"发布完成的
   * 先后"，同一发布者内部严格保序；一个还没发布的槽不会挡住其他就绪槽。
   * Slots are selected by publish sequence (wraparound-safe comparison), so the
   * dequeue order is publish-completion order and each publisher preserves its
   * own order; a slot that has not been published yet never hides other ready
   * slots.
   */
  ErrorCode Take(size_t& index);
  /**
   * @brief 释放自己持有的槽（写入中或读取中 → 空闲）
   *        / Release an owned slot (writing or reading -> free).
   * @param index 自己持有的槽位下标 / Slot index owned by the caller
   *
   * 释放不会失败；放弃一个只 `Acquire` 未发布的槽同样是合法用法
   * （分配器模式）。释放其他执行者持有的槽是调用方错误。
   * Releasing cannot fail; abandoning a slot that was only acquired (allocator
   * usage) is also valid. Releasing a slot owned by another context is a caller
   * error.
   */
  void Release(size_t index);
  /**
   * @brief 获取槽总数 / Get the total number of slots.
   * @return 槽总数 / Total number of slots
   */
  [[nodiscard]] size_t Size() const { return slot_count_; }

 protected:
  /// @brief 获取指定槽位的存储地址 / Get the storage address of one slot.
  [[nodiscard]] void* SlotPtr(size_t index);
  /// @brief 获取指定槽位的存储地址（只读）/ Get the storage address of one slot (const).
  [[nodiscard]] const void* SlotPtr(size_t index) const;

  /// 无效槽位下标 / Sentinel for "no slot chosen".
  static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

  /// @brief 槽状态 / Slot state.
  enum SlotState : uint32_t
  {
    SLOT_FREE = 0,     ///< 空闲，可被 `Acquire` 认领 / Free, claimable by `Acquire`
    SLOT_WRITING = 1,  ///< 写入中，已被生产者持有 / Writing, owned by a producer
    SLOT_READY = 2,    ///< 就绪，等待 `Take` / Ready, waiting for `Take`
    SLOT_READING = 3   ///< 读取中，已被消费者持有 / Reading, owned by a consumer
  };

  const size_t slot_size_;    ///< 单个槽的字节数 / Byte size of one slot.
  const size_t slot_count_;   ///< 槽总数 / Total number of slots.
  const size_t slot_stride_;  ///< 相邻槽位之间的步长 / Byte stride between slots.
  const size_t alloc_align_;  ///< 槽存储的分配对齐 / Allocation alignment of the slot
                              ///< storage.
  std::atomic<uint32_t>* states_;  ///< 每槽状态字 / Per-slot atomic state word.
  std::atomic<uint32_t>* seqs_;    ///< 每槽发布序号 / Per-slot publish sequence.
  std::byte* slots_;               ///< 槽存储缓冲区 / Byte buffer storing slots.

  /// 发布序号计数器，兼作 `Take` 重扫的发布计数 / Publish counter, also the
  /// change detector for the `Take` rescan.
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<uint32_t> publish_seq_;
  /// 释放计数，`Acquire` 重扫的变化检测 / Release counter, the change detector
  /// for the `Acquire` rescan.
  alignas(LibXR::CONCURRENCY_ALIGNMENT) std::atomic<uint32_t> release_count_;

 private:
  /// @brief 判定序号 `a` 是否早于 `b`（带符号差值，允许回绕）
  ///        / Whether sequence `a` is older than `b` (signed delta, wraparound-safe).
  static bool SeqBefore(uint32_t a, uint32_t b)
  {
    return static_cast<int32_t>(a - b) < 0;
  }

  /// @brief 安全地向上对齐字节数 / Align one byte count upward safely.
  [[nodiscard]] static size_t AlignUp(size_t value, size_t align);

  SlotPoolBase(const SlotPoolBase&);
  SlotPoolBase& operator=(const SlotPoolBase&);
  SlotPoolBase(SlotPoolBase&&);
  SlotPoolBase& operator=(SlotPoolBase&&);
};

/**
 * @class SlotPool
 * @brief 面向任务与中断的小容量消息槽池。
 *        Small message slot pool shared between tasks and interrupts.
 *
 * 每个槽一个原子状态（空闲、写入中、就绪、读取中），所有状态转换用 CAS
 * 完成；`Publish` 时取全局递增的发布序号，`Take` 选序号最小的就绪槽。
 * 适用于几到几十个槽的小容量场合，`Acquire`/`Take` 是 O(N) 扫描。
 * 一个被打断的操作只占住它自己的槽：任务停在 `Acquire`、写入、`Publish`、
 * `Take`、读取、`Release` 的任何中间点时，中断里的 `Put`/`Get` 都不会被挡住，
 * 拿到的 FULL/EMPTY 反映那一刻的真实状态。多生产者多消费者均可用；请勿在
 * NMI 中使用。出队顺序是发布完成的先后，同一发布者内部保序；不保证跨发布
 * 者的全局先进先出。
 *
 * Each slot carries one atomic state (free, writing, ready, reading) and every
 * state transition is a CAS; `Publish` takes a globally increasing sequence and
 * `Take` picks the ready slot with the smallest sequence. It targets small pools
 * of a few to a few dozen slots; `Acquire`/`Take` are O(N) scans. An interrupted
 * operation only occupies its own slot: while a task stops at any intermediate
 * point of `Acquire`, writing, `Publish`, `Take`, reading or `Release`, a
 * `Put`/`Get` from an interrupt is never blocked by it, and the FULL/EMPTY it
 * observes reflects the true state at that moment. Any number of producers and
 * consumers may run concurrently; do not use it inside NMI. Dequeue order is
 * publish-completion order, order inside one publisher is preserved, and no
 * global cross-publisher FIFO is guaranteed.
 *
 * @tparam T 槽存储的数据类型 / Slot element type.
 */
template <typename T>
class SlotPool : public SlotPoolBase
{
 public:
  /**
   * @brief 构造槽池 / Construct one slot pool.
   * @param slot_count 槽总数 / Total number of slots
   *
   * @note 包含构造期内存分配，运行期不再分配 / Allocates at construction only;
   *       no allocation at run time.
   */
  explicit SlotPool(size_t slot_count)
      : SlotPoolBase(sizeof(T), alignof(T), slot_count)
  {
  }

  /**
   * @brief 访问自己持有的槽（写入中或读取中）/ Access an owned slot (writing or
   *        reading).
   * @param index 自己持有的槽位下标 / Slot index owned by the caller
   * @return 槽内数据的引用 / Reference to the slot data
   */
  T& At(size_t index) { return *static_cast<T*>(SlotPtr(index)); }
  /// @brief 只读访问自己持有的槽 / Read-only access to an owned slot.
  const T& At(size_t index) const { return *static_cast<const T*>(SlotPtr(index)); }

  /**
   * @brief 便捷入队：`Acquire` + 拷贝 + `Publish` / Convenient enqueue:
   *        `Acquire` + copy + `Publish`.
   * @param value 待入队的数据 / Data to enqueue
   * @return 成功返回 `ErrorCode::OK`；没有空闲槽返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; `ErrorCode::FULL` when no slot
   *         is free
   * @note 要求 `T` 平凡可拷贝 / Requires `T` to be trivially copyable.
   */
  ErrorCode Put(const T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SlotPool::Put requires a trivially copyable type");
    size_t index = kInvalidIndex;
    const ErrorCode ec = Acquire(index);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    Memory::FastCopy(SlotPtr(index), &value, sizeof(T));
    Publish(index);
    return ErrorCode::OK;
  }

  /**
   * @brief 便捷出队：`Take` + 拷贝 + `Release` / Convenient dequeue:
   *        `Take` + copy + `Release`.
   * @param value 接收数据的缓冲区 / Buffer receiving the data
   * @return 成功返回 `ErrorCode::OK`；没有就绪槽返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; `ErrorCode::EMPTY` when no slot
   *         is ready
   * @note 要求 `T` 平凡可拷贝 / Requires `T` to be trivially copyable.
   */
  ErrorCode Get(T& value)
  {
    static_assert(std::is_trivially_copyable_v<T>,
                  "SlotPool::Get requires a trivially copyable type");
    size_t index = kInvalidIndex;
    const ErrorCode ec = Take(index);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    Memory::FastCopy(&value, SlotPtr(index), sizeof(T));
    Release(index);
    return ErrorCode::OK;
  }
};
}  // namespace LibXR
