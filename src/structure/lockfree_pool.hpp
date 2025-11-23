#pragma once

#include <atomic>
#include <new>

#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @class LockFreePool
 * @brief 无锁无序槽池 / Lock-free, unordered slot pool
 *
 * 该类实现了一个固定容量的无锁无序对象池，允许多个线程安全地并发存入（Put）和取出（Get）元素。每个槽独立管理，无严格顺序，适用于高性能、多线程或中断场景的数据缓存、对象重用等需求。
 *
 * This class implements a lock-free, unordered slot/object pool with a fixed size.
 * Multiple threads can safely Put (store) and Get (retrieve) elements concurrently.
 * Each slot is managed independently, making it ideal for high-performance, multi-thread,
 * or interrupt-safe buffering and object reuse.
 *
 * @tparam Data 槽中存储的数据类型 / Type of data stored in each slot.
 */
template <typename Data>
class LockFreePool
{
 public:
  // NOLINTBEGIN
  /// @brief 槽状态 / Slot state
  enum class SlotState : uint32_t
  {
    FREE = 0,             ///< 空闲，可写 / Slot is free and can be written.
    BUSY = 1,             ///< 正在写入 / Slot is being written.
    READY = 2,            ///< 写入完成，可读 / Slot contains valid data, ready to read.
    RECYCLE = UINT32_MAX  ///< 已读待回收 / Slot was read, waiting for next write.

  };
  // NOLINTEND

  /// @brief 单个槽结构体 / Individual slot structure (cache line aligned)
  union alignas(LIBXR_CACHE_LINE_SIZE) Slot
  {
    struct
    {
      std::atomic<SlotState> state;  ///< 当前槽状态 / Current state
      Data data;                     ///< 槽内数据 / Stored data
    } slot;

    uint8_t pad[LIBXR_CACHE_LINE_SIZE];  ///< 缓存行填充 / Cache line padding
  };

  /**
   * @brief 构造对象池 / Constructor for the pool
   * @param slot_count 槽数量 / Number of slots in the pool
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  LockFreePool(uint32_t slot_count)
      : SLOT_COUNT(slot_count),
        slots_(new(std::align_val_t(LIBXR_CACHE_LINE_SIZE)) Slot[slot_count])
  {
    for (uint32_t index = 0; index < SLOT_COUNT; index++)
    {
      slots_[index].slot.state.store(SlotState::FREE, std::memory_order_relaxed);
    }
  }

  /**
   * @brief 析构，释放槽池内存 / Destructor, releasing pool memory
   */
  ~LockFreePool() { delete[] slots_; }

  /**
   * @brief 向池中放入一个元素 / Put an element into the pool
   * @param data 要存储的数据 / Data to store
   * @return 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功放入 / Successfully put
   *         - `ErrorCode::FULL`：池满，无法放入 / Pool full, cannot put
   *
   */
  ErrorCode Put(const Data &data)
  {
    uint32_t start_index = 0;
    return Put(data, start_index);
  }

  /**
   * @brief 向池中放入一个元素，返回起始槽索引 / Put an element into the pool and return
   * the starting slot index
   *
   * @param data 要存储的数据 / Data to store
   * @param start_index 起始槽索引 / Starting slot index
   * @return ErrorCode 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功放入 / Successfully put
   *         - `ErrorCode::FULL`：池满，无法放入 / Pool full, cannot put
   */
  ErrorCode Put(const Data &data, uint32_t &start_index)
  {
    for (uint32_t index = start_index; index < SLOT_COUNT; index++)
    {
      auto expected = slots_[index].slot.state.load(std::memory_order_relaxed);
      if (expected == SlotState::FREE || expected == SlotState::RECYCLE)
      {
        if (slots_[index].slot.state.compare_exchange_strong(expected, SlotState::BUSY,
                                                             std::memory_order_release,
                                                             std::memory_order_relaxed))
        {
          slots_[index].slot.data = data;
          slots_[index].slot.state.store(SlotState::READY, std::memory_order_release);
          start_index = index;
          return ErrorCode::OK;
        }
      }
    }
    return ErrorCode::FULL;
  }

  /**
   * @brief 向指定槽放入一个元素 / Put an element into a specific slot
   *
   * @param data 要存储的数据 / Data to store
   * @param index 槽索引 / Slot index
   * @return ErrorCode 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功放入 / Successfully put
   *         - `ErrorCode::FULL`：池满，无法放入 / Pool full, cannot put
   */
  ErrorCode PutToSlot(const Data &data, uint32_t index)
  {
    auto expected = slots_[index].slot.state.load(std::memory_order_relaxed);
    if (expected == SlotState::FREE || expected == SlotState::RECYCLE)
    {
      if (slots_[index].slot.state.compare_exchange_strong(expected, SlotState::BUSY,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed))
      {
        slots_[index].slot.data = data;
        slots_[index].slot.state.store(SlotState::READY, std::memory_order_release);
        return ErrorCode::OK;
      }
    }
    return ErrorCode::FULL;
  }

  /**
   * @brief 从池中取出一个元素 / Retrieve an element from the pool
   * @param[out] data 获取到的数据 / Variable to store the retrieved data
   * @return 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功取出 / Successfully retrieved
   *         - `ErrorCode::EMPTY`：池空，无可取元素 / Pool empty, no available element
   *
   */
  ErrorCode Get(Data &data)
  {
    uint32_t start_index = 0;

    return Get(data, start_index);
  }

  /**
   * @brief 从指定槽位开始，取出一个元素 / Retrieve an element from the pool
   * @param[out] data 获取到的数据 / Variable to store the retrieved data
   * @param[in,out] start_index 本次起始槽，返回本次实际取出槽位置
   *                            / Starting slot index, returned with the actual slot index
   * @return 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功取出 / Successfully retrieved
   *         - `ErrorCode::EMPTY`：池空，无可取元素 / Pool empty, no available element
   *
   */
  ErrorCode Get(Data &data, uint32_t &start_index)
  {
    for (uint32_t index = start_index; index < SLOT_COUNT; index++)
    {
      auto expected = slots_[index].slot.state.load(std::memory_order_acquire);
      if (expected == SlotState::READY)
      {
        if (slots_[index].slot.state.compare_exchange_strong(expected, SlotState::BUSY,
                                                             std::memory_order_acquire,
                                                             std::memory_order_relaxed))
        {
          data = slots_[index].slot.data;
          slots_[index].slot.state.store(SlotState::RECYCLE, std::memory_order_release);
          start_index = index;
          return ErrorCode::OK;
        }
      }
      if (expected == SlotState::FREE)
      {
        start_index = 0;
        return ErrorCode::EMPTY;
      }
    }

    start_index = 0;
    return ErrorCode::EMPTY;
  }

  /**
   * @brief 从指定槽位开始，取出一个元素 / Retrieve an element from the pool
   *
   * @param data 获取到的数据 / Variable to store the retrieved data
   * @param index 槽索引 / Slot index
   * @return ErrorCode 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功取出 / Successfully retrieved
   *         - `ErrorCode::EMPTY`：池空，无可取元素 / Pool empty, no available element
   */
  ErrorCode GetFromSlot(Data &data, uint32_t index)
  {
    auto expected = slots_[index].slot.state.load(std::memory_order_acquire);
    if (expected == SlotState::READY)
    {
      if (slots_[index].slot.state.compare_exchange_strong(expected, SlotState::BUSY,
                                                           std::memory_order_acquire,
                                                           std::memory_order_relaxed))
      {
        data = slots_[index].slot.data;
        slots_[index].slot.state.store(SlotState::RECYCLE, std::memory_order_release);
        return ErrorCode::OK;
      }
    }
    return ErrorCode::EMPTY;
  }

  /**
   * @brief 回收指定槽位 / Recycle a slot
   *
   * @param index 槽索引 / Slot index
   * @return ErrorCode 操作结果 / Operation result:
   *         - `ErrorCode::OK`：成功回收 / Successfully recycled
   *         - `ErrorCode::EMPTY`：池空，无可回收元素 / Pool empty, no available element
   */
  ErrorCode RecycleSlot(uint32_t index)
  {
    auto expected = slots_[index].slot.state.load(std::memory_order_relaxed);
    if (expected == SlotState::READY)
    {
      if (slots_[index].slot.state.compare_exchange_strong(expected, SlotState::RECYCLE,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed))
      {
        return ErrorCode::OK;
      }
    }
    return ErrorCode::EMPTY;
  }

  /**
   * @brief 查询池中可取元素数量 / Query the number of available elements in the pool
   * @return 当前池中READY元素个数 / Number of ready elements in pool
   */
  [[nodiscard]] size_t Size() const
  {
    uint32_t size = 0;
    for (uint32_t index = 0; index < SLOT_COUNT; index++)
    {
      if (slots_[index].slot.state.load(std::memory_order_relaxed) == SlotState::READY)
      {
        size++;
      }
    }
    return size;
  }

  /**
   * @brief 查询当前池可用槽数量 / Query the number of writable slots in the pool
   * @return 可写入的空槽数 / Number of writable slots in pool
   */
  [[nodiscard]] size_t EmptySize()
  {
    uint32_t size = 0;
    for (uint32_t index = 0; index < SLOT_COUNT; index++)
    {
      auto state = slots_[index].slot.state.load(std::memory_order_relaxed);
      if (state == SlotState::FREE || state == SlotState::RECYCLE)
      {
        size++;
      }
    }
    return size;
  }

  /**
   * @brief 获取槽总数 / Get the total number of slots in the pool
   *
   * @return uint32_t 槽总数
   */
  uint32_t SlotCount() const { return SLOT_COUNT; }

 protected:
  Slot &operator[](uint32_t index)
  {
    if (index >= SLOT_COUNT)
    {
      ASSERT(false);
    }
    return slots_[index];
  }

 private:
  const uint32_t SLOT_COUNT;  ///< 槽总数 / Number of slots
  Slot *slots_;               ///< 槽数组指针 / Array of slots
};
}  // namespace LibXR
