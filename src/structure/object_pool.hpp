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
/**
 * @brief 可作为对象池空闲索引队列的类型约束。
 * @brief Type constraint for free-index queues used by object pools.
 *
 * 该约束只要求队列提供最小的强类型接口：
 * - `ValueType`
 * - `Push(const ValueType&)`
 * - `Pop(ValueType&)`
 * - `Pop()`
 *
 * This constraint requires only the minimal typed queue interface:
 * - `ValueType`
 * - `Push(const ValueType&)`
 * - `Pop(ValueType&)`
 * - `Pop()`
 */
template <typename QueueType>
concept PoolIndexQueue = requires(QueueType queue,
                                  const typename QueueType::ValueType& index_const,
                                  typename QueueType::ValueType& index_mut)
{
  typename QueueType::ValueType;
  { queue.Push(index_const) } -> std::same_as<ErrorCode>;
  { queue.Pop(index_mut) } -> std::same_as<ErrorCode>;
  { queue.Pop() } -> std::same_as<ErrorCode>;
  { queue.Size() } -> std::convertible_to<size_t>;
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

  /// @brief 槽索引必须是整数类型。 Slot indices must use an integral type.
  static_assert(std::is_integral_v<IndexType>,
                "BasicObjectPool requires an integral index queue");
  /// @brief 槽索引必须是无符号整数类型。 Slot indices must use an unsigned integral type.
  static_assert(std::is_unsigned_v<IndexType>,
                "BasicObjectPool requires an unsigned index queue");

  /**
   * @class Handle
   * @brief 对象池槽位的 move-only RAII 句柄。
   * @brief Move-only RAII handle for one object-pool slot.
   *
   * 句柄持有一个槽位索引及所属 pool 指针；析构时会自动把索引归还到空闲队列。
   * The handle stores one slot index plus its owning pool pointer, and returns
   * the index to the free queue automatically on destruction.
   */
  class Handle
  {
   public:
    /**
     * @brief 构造一个空 handle。
     * @brief Construct an empty handle.
     */
    Handle() = default;

    /**
     * @brief 用指定 pool 和槽位索引构造 handle。
     * @brief Construct a handle from the given pool and slot index.
     * @param pool 所属对象池。 Owning object pool.
     * @param index 槽位索引。 Slot index.
     */
    Handle(BasicObjectPool* pool, IndexType index) : pool_(pool), index_(index) {}

    /// @brief 禁止拷贝构造。 Non-copyable.
    Handle(const Handle&) = delete;
    /// @brief 禁止拷贝赋值。 Non-copy-assignable.
    Handle& operator=(const Handle&) = delete;

    /**
     * @brief 移动构造 handle，并转移槽位所有权。
     * @brief Move-construct the handle and transfer slot ownership.
     * @param other 被转移的源 handle。 Source handle being moved from.
     */
    Handle(Handle&& other) noexcept
        : pool_(std::exchange(other.pool_, nullptr)),
          index_(std::exchange(other.index_, IndexType{}))
    {
    }

    /**
     * @brief 移动赋值 handle，并转移槽位所有权。
     * @brief Move-assign the handle and transfer slot ownership.
     * @param other 被转移的源 handle。 Source handle being moved from.
     * @return 当前 handle 的引用。 Reference to this handle.
     */
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

    /**
     * @brief 析构 handle，并自动归还槽位。
     * @brief Destroy the handle and return the slot automatically.
     */
    ~Handle() { Reset(); }

    /**
     * @brief 判断当前 handle 是否持有有效槽位。
     * @brief Return whether this handle currently owns a valid slot.
     * @return 持有有效槽位返回 `true`，否则返回 `false`。
     *         Returns `true` when this handle owns a valid slot, otherwise `false`.
     */
    [[nodiscard]] bool Valid() const { return pool_ != nullptr; }

    /**
     * @brief 返回当前槽位对象的可写引用。
     * @brief Return a writable reference to the current slot object.
     * @return 当前槽位对象引用。 Reference to the current slot object.
     */
    [[nodiscard]] Data& Get()
    {
      ASSERT(pool_ != nullptr);
      return pool_->slots_[index_];
    }

    /**
     * @brief 返回当前槽位对象的只读引用。
     * @brief Return a read-only reference to the current slot object.
     * @return 当前槽位对象常量引用。 Const reference to the current slot object.
     */
    [[nodiscard]] const Data& Get() const
    {
      ASSERT(pool_ != nullptr);
      return pool_->slots_[index_];
    }

    /**
     * @brief 以指针形式访问当前槽位对象。
     * @brief Access the current slot object as a pointer.
     * @return 指向当前槽位对象的指针。 Pointer to the current slot object.
     */
    [[nodiscard]] Data* operator->() { return &Get(); }

    /**
     * @brief 以只读指针形式访问当前槽位对象。
     * @brief Access the current slot object as a const pointer.
     * @return 指向当前槽位对象的只读指针。 Const pointer to the current slot object.
     */
    [[nodiscard]] const Data* operator->() const { return &Get(); }

    /**
     * @brief 解引用当前槽位对象。
     * @brief Dereference the current slot object.
     * @return 当前槽位对象引用。 Reference to the current slot object.
     */
    [[nodiscard]] Data& operator*() { return Get(); }

    /**
     * @brief 只读解引用当前槽位对象。
     * @brief Dereference the current slot object as const.
     * @return 当前槽位对象常量引用。 Const reference to the current slot object.
     */
    [[nodiscard]] const Data& operator*() const { return Get(); }

    /**
     * @brief 返回当前持有的槽位索引。
     * @brief Return the currently owned slot index.
     * @return 当前槽位索引。 Current slot index.
     */
    [[nodiscard]] IndexType Index() const
    {
      ASSERT(pool_ != nullptr);
      return index_;
    }

    /**
     * @brief 主动归还当前槽位，并使 handle 失效。
     * @brief Return the current slot explicitly and invalidate the handle.
     */
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
    BasicObjectPool* pool_ = nullptr;  ///< 所属对象池。 Owning object pool.
    IndexType index_ = {};             ///< 当前槽位索引。 Current slot index.
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
    ASSERT(slot_count_ > 0);
    ConstructOwnedQueue(slot_count_);
    AllocateOwnedSlots();
    InitializeFreeQueue();
  }

  /**
   * @brief 用内部 queue 和外部 slots 构造 pool。
   * @brief Construct the pool with an internal queue and external slots.
   * @param slot_count 槽位数量。 Number of slots.
   * @param slots 外部槽数组。 Caller-provided slot storage.
   *
   * @note 调用方必须保证 `slots` 指向至少 `slot_count` 个 `Data` 槽位。
   *       The caller must ensure that `slots` points to at least `slot_count`
   *       `Data` slots.
   */
  BasicObjectPool(size_t slot_count, Data* slots)
      : slot_count_(slot_count), slots_(slots)
  {
    ASSERT(slot_count_ > 0);
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
    ASSERT(slot_count_ > 0);
    ASSERT(free_queue.Size() == 0);
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
   * @note 调用方必须保证 `slots` 指向至少 `slot_count` 个 `Data` 槽位。
   *       The caller must ensure that `slots` points to at least `slot_count`
   *       `Data` slots.
   */
  BasicObjectPool(FreeQueue& free_queue, size_t slot_count, Data* slots)
      : free_queue_(&free_queue), slot_count_(slot_count), slots_(slots)
  {
    ASSERT(slot_count_ > 0);
    ASSERT(slots_ != nullptr);
    ASSERT(free_queue.Size() == 0);
    InitializeFreeQueue();
  }

  /**
   * @brief 析构对象池，并释放其拥有的内部资源。
   * @brief Destroy the object pool and release owned internal resources.
   */
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

  /// @brief 禁止拷贝构造。 Non-copyable.
  BasicObjectPool(const BasicObjectPool&) = delete;
  /// @brief 禁止拷贝赋值。 Non-copy-assignable.
  BasicObjectPool& operator=(const BasicObjectPool&) = delete;
  /// @brief 禁止移动构造。 Non-movable.
  BasicObjectPool(BasicObjectPool&&) = delete;
  /// @brief 禁止移动赋值。 Non-move-assignable.
  BasicObjectPool& operator=(BasicObjectPool&&) = delete;

  /**
   * @brief 获取一个槽位 handle。
   * @brief Acquire one slot handle.
   * @param handle 用于接收成功获取的 handle。 Handle receiving the acquired slot.
   * @return 成功返回 `ErrorCode::OK`，池空返回 `ErrorCode::EMPTY`。
   *         Returns `ErrorCode::OK` on success and `ErrorCode::EMPTY` when the pool is empty.
   */
  [[nodiscard]] ErrorCode Acquire(Handle& handle)
  {
    ASSERT(!handle.Valid());

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

  /**
   * @brief 返回当前仍可获取的空闲槽位数。
   * @brief Return the number of currently acquirable free slots.
   * @return 当前空闲槽位数。 Current number of free slots.
   */
  [[nodiscard]] size_t EmptySize() const { return free_queue_->Size(); }

  /**
   * @brief 返回对象池总槽位数。
   * @brief Return the total number of slots in the pool.
   * @return 槽位总数。 Total slot count.
   */
  [[nodiscard]] size_t Size() const { return slot_count_; }

  /**
   * @brief 通过槽位索引访问对象。
   * @brief Access an object by slot index.
   * @param index 槽位索引。 Slot index.
   * @return 对应槽位对象的引用。 Reference to the object stored in the slot.
   */
  [[nodiscard]] Data& operator[](size_t index)
  {
    ASSERT(index < slot_count_);
    return slots_[index];
  }

  /**
   * @brief 通过槽位索引只读访问对象。
   * @brief Access an object by slot index as const.
   * @param index 槽位索引。 Slot index.
   * @return 对应槽位对象的常量引用。 Const reference to the object stored in the slot.
   */
  [[nodiscard]] const Data& operator[](size_t index) const
  {
    ASSERT(index < slot_count_);
    return slots_[index];
  }

 private:
  /**
   * @brief 把指定槽位索引归还到空闲队列。
   * @brief Return the given slot index back to the free queue.
   * @param index 待归还的槽位索引。 Slot index to return.
   * @return 底层空闲队列返回的结果码。 Result code returned by the underlying free queue.
   */
  ErrorCode Release(IndexType index)
  {
    ASSERT(static_cast<size_t>(index) < slot_count_);
    return free_queue_->Push(index);
  }

  /**
   * @brief 在内部存储区里构造一个自拥有空闲队列。
   * @brief Construct an owned free queue inside internal storage.
   * @param slot_count 槽位数量，同时也是初始化时要压入的索引数量。
   *                   Slot count and thus the number of indices to preload.
   */
  void ConstructOwnedQueue(size_t slot_count)
  {
    free_queue_ = new (free_queue_storage_) FreeQueue(slot_count);
    owns_free_queue_ = true;
  }

  /**
   * @brief 申请并拥有内部槽数组。
   * @brief Allocate and own the internal slot storage.
   */
  void AllocateOwnedSlots()
  {
    slots_ = new Data[slot_count_];
    owns_slots_ = true;
  }

  /**
   * @brief 用 `0 .. slot_count - 1` 初始化空闲索引队列。
   * @brief Initialize the free-index queue with `0 .. slot_count - 1`.
   */
  void InitializeFreeQueue()
  {
    ASSERT(slot_count_ > 0);
    ASSERT(slot_count_ - 1 <=
           static_cast<size_t>(std::numeric_limits<IndexType>::max()));
    for (size_t index = 0; index < slot_count_; ++index)
    {
      const ErrorCode ec = free_queue_->Push(static_cast<IndexType>(index));
      ASSERT(ec == ErrorCode::OK);
    }
  }

  /// @brief 内部自拥有 queue 的原地构造存储区。 In-place storage for an internally owned queue.
  alignas(FreeQueue) std::byte free_queue_storage_[sizeof(FreeQueue)] = {};
  FreeQueue* free_queue_ = nullptr; ///< 空闲索引队列指针。 Pointer to the free-index queue.
  const size_t slot_count_;         ///< 槽位总数。 Total slot count.
  Data* slots_ = nullptr;           ///< 槽数组指针。 Pointer to slot storage.
  bool owns_free_queue_ = false;    ///< 是否拥有内部 queue。 Whether this pool owns the free queue.
  bool owns_slots_ = false;         ///< 是否拥有内部 slots。 Whether this pool owns the slot storage.
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
