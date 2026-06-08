#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#include "libxr_def.hpp"
#include "queue.hpp"

namespace LibXR
{
template <typename QueueType>
concept PoolIndexQueue = requires(QueueType queue,
                                  const typename QueueType::ValueType& index_const,
                                  typename QueueType::ValueType& index_mut)
{
  typename QueueType::ValueType;
  { queue.Push(index_const) } -> std::same_as<ErrorCode>;
  { queue.Pop(index_mut) } -> std::same_as<ErrorCode>;
  { queue.Pop() } -> std::same_as<ErrorCode>;
};

/**
 * @class BasicObjectPool
 * @brief 基于空闲索引队列的 RAII 槽池。
 * @brief RAII slot pool backed by a free-index queue.
 *
 * 该池使用一个索引队列管理空闲槽位；成功获取后返回 move-only `Handle`，其析构会
 * 自动把槽位索引归还到空闲队列。这样上层只依赖四类队列共享的最小 typed 接口。
 *
 * This pool uses one index queue to manage free slots. Successful acquisition
 * returns one move-only `Handle`, whose destructor automatically returns the
 * slot index back to the free queue. This keeps the upper layer dependent only
 * on the minimal typed interface shared by the queue family.
 *
 * @tparam Data 槽内对象类型。 Slot object type.
 * @tparam FreeQueue 空闲索引队列类型。 Free-index queue type.
 */
template <typename Data, PoolIndexQueue FreeQueue>
class BasicObjectPool
{
 public:
  using ValueType = Data;                           ///< 槽内对象类型。 Slot object type.
  using QueueType = FreeQueue;                     ///< 空闲索引队列类型。 Free-index queue type.
  using IndexType = typename FreeQueue::ValueType; ///< 槽索引类型。 Slot index type.

  static_assert(std::is_integral_v<IndexType>,
                "BasicObjectPool requires an integral index queue");
  static_assert(std::is_unsigned_v<IndexType>,
                "BasicObjectPool requires an unsigned index queue");

  class Handle
  {
   public:
    Handle() = default;

    Handle(BasicObjectPool* pool, IndexType index) : pool_(pool), index_(index) {}

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr)),
          index_(std::exchange(other.index_, IndexType{}))
    {
    }

    Handle& operator=(Handle&& other) noexcept
    {
      if (this == &other)
      {
        return *this;
      }

      Reset();
      pool_ = std::exchange(other.pool_, nullptr);
      index_ = std::exchange(other.index_, IndexType{});
      return *this;
    }

    ~Handle() { Reset(); }

    [[nodiscard]] bool Valid() const { return pool_ != nullptr; }

    [[nodiscard]] Data& Get()
    {
      ASSERT(pool_ != nullptr);
      return pool_->slots_[index_];
    }

    [[nodiscard]] const Data& Get() const
    {
      ASSERT(pool_ != nullptr);
      return pool_->slots_[index_];
    }

    [[nodiscard]] Data* operator->() { return &Get(); }
    [[nodiscard]] const Data* operator->() const { return &Get(); }
    [[nodiscard]] Data& operator*() { return Get(); }
    [[nodiscard]] const Data& operator*() const { return Get(); }

    [[nodiscard]] IndexType Index() const
    {
      ASSERT(pool_ != nullptr);
      return index_;
    }

    void Reset()
    {
      if (pool_ == nullptr)
      {
        return;
      }

      const ErrorCode ec = pool_->Release(index_);
      ASSERT(ec == ErrorCode::OK);
      pool_ = nullptr;
      index_ = IndexType{};
    }

   private:
    BasicObjectPool* pool_ = nullptr;  ///< 所属 pool。 Owning pool.
    IndexType index_ = {};      ///< 槽位索引。 Slot index.
  };

  /**
   * @brief 用内部 queue 和内部 slots 构造 pool。
   * @brief Construct the pool with an internal queue and internal slots.
   * @param slot_count 槽位数量。 Number of slots.
   */
  template <typename T = Data>
  requires std::is_default_constructible_v<T>
  explicit BasicObjectPool(size_t slot_count) : slot_count_(slot_count)
  {
    ConstructOwnedQueue(slot_count_);
    AllocateOwnedSlots();
    InitializeFreeQueue();
  }

  /**
   * @brief 用内部 queue 和外部 slots 构造 pool。
   * @brief Construct the pool with an internal queue and external slots.
   * @param slot_count 槽位数量。 Number of slots.
   * @param slots 外部槽数组。 Caller-provided slot storage.
   */
  BasicObjectPool(size_t slot_count, Data* slots)
      : slot_count_(slot_count), slots_(slots)
  {
    ASSERT(slots_ != nullptr);
    ConstructOwnedQueue(slot_count_);
    InitializeFreeQueue();
  }

  /**
   * @brief 用外部 queue 和内部 slots 构造 pool。
   * @brief Construct the pool with an external queue and internal slots.
   * @param free_queue 外部空闲索引队列。 Caller-provided free-index queue.
   * @param slot_count 槽位数量。 Number of slots.
   *
   * @note 调用方传入的 `free_queue` 必须是空队列，并且只供当前 pool 独占使用。
   *       The caller-provided `free_queue` must be empty and dedicated to this pool.
   */
  template <typename T = Data>
  requires std::is_default_constructible_v<T>
  BasicObjectPool(FreeQueue& free_queue, size_t slot_count)
      : free_queue_(&free_queue), slot_count_(slot_count)
  {
    AllocateOwnedSlots();
    InitializeFreeQueue();
  }

  /**
   * @brief 用外部 queue 和外部 slots 构造 pool。
   * @brief Construct the pool with an external queue and external slots.
   * @param free_queue 外部空闲索引队列。 Caller-provided free-index queue.
   * @param slot_count 槽位数量。 Number of slots.
   * @param slots 外部槽数组。 Caller-provided slot storage.
   *
   * @note 调用方传入的 `free_queue` 必须是空队列，并且只供当前 pool 独占使用。
   *       The caller-provided `free_queue` must be empty and dedicated to this pool.
   */
  BasicObjectPool(FreeQueue& free_queue, size_t slot_count, Data* slots)
      : free_queue_(&free_queue), slot_count_(slot_count), slots_(slots)
  {
    ASSERT(slots_ != nullptr);
    InitializeFreeQueue();
  }

  ~BasicObjectPool()
  {
    if (owns_slots_)
    {
      delete[] slots_;
    }

    if (owns_free_queue_)
    {
      free_queue_->~FreeQueue();
    }
  }

  BasicObjectPool(const BasicObjectPool&) = delete;
  BasicObjectPool& operator=(const BasicObjectPool&) = delete;
  BasicObjectPool(BasicObjectPool&&) = delete;
  BasicObjectPool& operator=(BasicObjectPool&&) = delete;

  ErrorCode Acquire(Handle& handle)
  {
    IndexType index = 0;
    const ErrorCode ec = free_queue_->Pop(index);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    ASSERT(static_cast<size_t>(index) < slot_count_);
    handle = Handle(this, index);
    return ErrorCode::OK;
  }

  [[nodiscard]] ErrorCode TryAcquire(Handle& handle) { return Acquire(handle); }

  [[nodiscard]] size_t EmptySize() const { return free_queue_->Size(); }
  [[nodiscard]] size_t Size() const { return slot_count_; }

  [[nodiscard]] Data& operator[](size_t index)
  {
    ASSERT(index < slot_count_);
    return slots_[index];
  }

  [[nodiscard]] const Data& operator[](size_t index) const
  {
    ASSERT(index < slot_count_);
    return slots_[index];
  }

 private:
  ErrorCode Release(IndexType index)
  {
    ASSERT(static_cast<size_t>(index) < slot_count_);
    return free_queue_->Push(index);
  }

  void ConstructOwnedQueue(size_t slot_count)
  {
    free_queue_ = new (free_queue_storage_) FreeQueue(slot_count);
    owns_free_queue_ = true;
  }

  void AllocateOwnedSlots()
  {
    slots_ = new Data[slot_count_];
    owns_slots_ = true;
  }

  void InitializeFreeQueue()
  {
    ASSERT(slot_count_ <= static_cast<size_t>(std::numeric_limits<IndexType>::max()) + 1U);
    for (size_t index = 0; index < slot_count_; ++index)
    {
      const ErrorCode ec = free_queue_->Push(static_cast<IndexType>(index));
      ASSERT(ec == ErrorCode::OK);
    }
  }

  alignas(FreeQueue) std::byte free_queue_storage_[sizeof(FreeQueue)] = {};
  FreeQueue* free_queue_ = nullptr; ///< 空闲索引队列指针。 Pointer to the free-index queue.
  const size_t slot_count_;        ///< 槽位总数。 Total slot count.
  Data* slots_ = nullptr;          ///< 槽数组。 Slot storage.
  bool owns_free_queue_ = false;   ///< 是否拥有内部 queue。 Whether this pool owns the free queue.
  bool owns_slots_ = false;        ///< 是否拥有内部 slots。 Whether this pool owns the slot storage.
};

/**
 * @brief 基于普通 FIFO 空闲索引队列的 RAII pool 别名。
 * @brief RAII pool alias backed by the ordinary FIFO free-index queue.
 * @tparam Data 槽内对象类型。 Slot object type.
 * @tparam IndexType 槽索引类型，默认 `uint32_t`。 Slot index type, default `uint32_t`.
 */
template <typename Data, typename IndexType = uint32_t>
using ObjectPool = BasicObjectPool<Data, Queue<IndexType>>;

/**
 * @brief 基于 SPSC 空闲索引队列的 RAII pool 别名。
 * @brief RAII pool alias backed by the SPSC free-index queue.
 * @tparam Data 槽内对象类型。 Slot object type.
 * @tparam IndexType 槽索引类型，默认 `uint32_t`。 Slot index type, default `uint32_t`.
 */
template <typename Data, typename IndexType = uint32_t>
using SPSCObjectPool = BasicObjectPool<Data, SPSCQueue<IndexType>>;

/**
 * @brief 基于 SPMC 空闲索引队列的 RAII pool 别名。
 * @brief RAII pool alias backed by the SPMC free-index queue.
 * @tparam Data 槽内对象类型。 Slot object type.
 * @tparam IndexType 槽索引类型，默认 `uint32_t`。 Slot index type, default `uint32_t`.
 */
template <typename Data, typename IndexType = uint32_t>
using SPMCObjectPool = BasicObjectPool<Data, SPMCQueue<IndexType>>;

/**
 * @brief 基于 MPMC 空闲索引队列的 RAII pool 别名。
 * @brief RAII pool alias backed by the MPMC free-index queue.
 * @tparam Data 槽内对象类型。 Slot object type.
 * @tparam IndexType 槽索引类型，默认 `uint32_t`。 Slot index type, default `uint32_t`.
 */
template <typename Data, typename IndexType = uint32_t>
using MPMCObjectPool = BasicObjectPool<Data, MPMCQueue<IndexType>>;
}  // namespace LibXR
