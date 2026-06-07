#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "spsc_queue_core.hpp"

namespace LibXR
{
/**
 * @class SPSCQueue
 * @brief 单生产者单消费者无锁队列 / Single-producer single-consumer lock-free queue
 */
template <typename Data>
class SPSCQueue
{
  /// @brief 仅接受可默认构造 payload / Accepts only default-constructible payloads.
  static_assert(std::is_default_constructible_v<Data>,
                "SPSCQueue requires default-constructible payloads");
  /// @brief 仅接受可析构 payload / Accepts only destructible payloads.
  static_assert(std::is_destructible_v<Data>,
                "SPSCQueue requires destructible payloads");
  /// @brief 仅接受可赋值 payload / Accepts only assignable payloads.
  static_assert(std::is_copy_assignable_v<Data> || std::is_move_assignable_v<Data>,
                "SPSCQueue requires assignable payloads");

 public:
  using ValueType = Data;  ///< 队列元素类型 / Queue element type.

  /**
   * @brief 构造一个 SPSC 队列 / Construct one SPSC queue
   * @param length 队列容量 / Queue capacity
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  explicit SPSCQueue(size_t length) : core_(sizeof(Data), length)
  {
    for (size_t index = 0; index < core_.RingCapacity(); ++index)
    {
      std::construct_at(SlotPtr(index));
    }
  }

  /**
   * @brief 析构一个 SPSC 队列 / Destroy one SPSC queue
   */
  ~SPSCQueue()
  {
    for (size_t index = 0; index < core_.RingCapacity(); ++index)
    {
      std::destroy_at(SlotPtr(index));
    }
  }

  /**
   * @brief 访问指定槽位中的 payload / Access the payload at one slot index
   * @param index 目标槽位下标 / Target slot index
   * @return 指向该槽位 payload 的指针 / Pointer to the payload stored in that slot
   */
  Data* operator[](size_t index) { return SlotPtr(index); }

  /**
   * @brief 向队列尾部推入一个 payload / Push one payload into the queue tail
   * @tparam ElementData 实参类型 / Argument type
   * @param item 待入队 payload / Payload to enqueue
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  template <typename ElementData = Data>
  ErrorCode Push(ElementData&& item)
  {
    static_assert(std::is_convertible_v<ElementData, Data>,
                  "SPSCQueue::Push element type must be convertible to Data");
    return core_.PushWithSlot(
        [&](std::byte* slot)
        {
          *reinterpret_cast<Data*>(slot) = std::forward<ElementData>(item);
          return ErrorCode::OK;
        });
  }

  /**
   * @brief 从队列头部弹出一个 payload / Pop one payload from the queue head
   * @param item 用于接收 payload / Receives the dequeued payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop(Data& item)
  {
    return core_.PopWithSlot(
        [&](const std::byte* slot)
        {
          item = *reinterpret_cast<const Data*>(slot);
          return ErrorCode::OK;
        });
  }

  /**
   * @brief 丢弃一个队头 payload / Discard one front payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop()
  {
    return core_.PopBytes(nullptr);
  }

  /**
   * @brief 查看一个队头 payload 但不出队 / Peek one front payload without dequeuing it
   * @param item 用于接收 payload / Receives the peeked payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Peek(Data& item)
  {
    return core_.PeekWithSlot(
        [&](const std::byte* slot)
        {
          item = *reinterpret_cast<const Data*>(slot);
          return ErrorCode::OK;
        });
  }

  /**
   * @brief 批量推入多个 payload / Push multiple payloads into the queue
   * @param data payload 数组指针 / Pointer to the payload array
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  ErrorCode PushBatch(const Data* data, size_t size)
  {
    if (data == nullptr && size > 0)
    {
      return ErrorCode::PTR_NULL;
    }
    auto current_tail = core_.LoadTailRelaxed();
    auto current_head = core_.LoadHeadAcquire();
    const size_t capacity = core_.RingCapacity();
    const size_t free_space = (current_tail >= current_head)
                                  ? (capacity - (current_tail - current_head) - 1)
                                  : (current_head - current_tail - 1);

    if (free_space < size)
    {
      return ErrorCode::FULL;
    }

    for (size_t index = 0; index < size; ++index)
    {
      *SlotPtr((current_tail + index) % static_cast<uint32_t>(capacity)) = data[index];
    }

    core_.StoreTailRelease((current_tail + size) % capacity);
    return ErrorCode::OK;
  }

  /**
   * @brief 通过写入器回调批量推入 payload / Push payloads via a writer callback
   * @tparam Writer 写入器类型 / Writer callback type
   * @param size 需要写入的 payload 个数 / Number of payloads to write
   * @param writer 写入器回调 / Writer callback
   * @return 返回写入结果 / Returns the writer result
   */
  template <typename Writer>
  ErrorCode PushWithWriter(size_t size, Writer&& writer)
  {
    for (size_t index = 0; index < size; ++index)
    {
      auto ec = core_.PushWithSlot(
          [&](std::byte* slot) -> ErrorCode
          {
            return writer(std::launder(reinterpret_cast<Data*>(slot)), 1);
          });
      if (ec != ErrorCode::OK)
      {
        return ec;
      }
    }
    return ErrorCode::OK;
  }

  /**
   * @brief 通过读取器回调批量弹出 payload / Pop payloads via a reader callback
   * @tparam Reader 读取器类型 / Reader callback type
   * @param size 需要读取的 payload 个数 / Number of payloads to read
   * @param reader 读取器回调 / Reader callback
   * @return 返回读取结果 / Returns the reader result
   */
  template <typename Reader>
  ErrorCode PopWithReader(size_t size, Reader&& reader)
  {
    for (size_t index = 0; index < size; ++index)
    {
      auto ec = core_.PopWithSlot(
          [&](const std::byte* slot) -> ErrorCode
          {
            return reader(std::launder(reinterpret_cast<const Data*>(slot)), 1);
          });
      if (ec != ErrorCode::OK)
      {
        return ec;
      }
    }
    return ErrorCode::OK;
  }

  /**
   * @brief 批量弹出多个 payload / Pop multiple payloads from the queue
   * @param data 用于接收 payload 的数组 / Array receiving dequeued payloads
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue does not contain enough payloads
   */
  ErrorCode PopBatch(Data* data, size_t size)
  {
    const auto current_head = core_.LoadHeadRelaxed();
    const auto current_tail = core_.LoadTailAcquire();
    const size_t capacity = core_.RingCapacity();
    const size_t available = (current_tail >= current_head)
                                 ? (current_tail - current_head)
                                 : (capacity - current_head + current_tail);

    if (available < size)
    {
      return ErrorCode::EMPTY;
    }

    if (data != nullptr)
    {
      for (size_t index = 0; index < size; ++index)
      {
        data[index] =
            *ConstSlotPtr((current_head + index) % static_cast<uint32_t>(capacity));
      }
    }

    core_.StoreHeadRelease((current_head + size) % capacity);
    return ErrorCode::OK;
  }

  /**
   * @brief 批量查看多个 payload 但不出队 / Peek multiple payloads without dequeuing them
   * @param data 用于接收 payload 的数组 / Array receiving peeked payloads
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue does not contain enough payloads
   */
  ErrorCode PeekBatch(Data* data, size_t size)
  {
    if (data == nullptr && size > 0)
    {
      return ErrorCode::PTR_NULL;
    }
    const auto current_head = core_.LoadHeadRelaxed();
    const auto current_tail = core_.LoadTailAcquire();
    const size_t capacity = core_.RingCapacity();
    const size_t available = (current_tail >= current_head)
                                 ? (current_tail - current_head)
                                 : (capacity - current_head + current_tail);

    if (available < size)
    {
      return ErrorCode::EMPTY;
    }

    for (size_t index = 0; index < size; ++index)
    {
      data[index] =
          *ConstSlotPtr((current_head + index) % static_cast<uint32_t>(capacity));
    }
    return ErrorCode::OK;
  }

  /**
   * @brief 重置队列状态 / Reset the queue state
   */
  void Reset() { core_.Reset(); }

  /**
   * @brief 获取当前已用元素数 / Get the current element count
   * @return 当前元素个数 / Current number of stored payloads
   */
  size_t Size() const { return core_.Size(); }

  /**
   * @brief 获取剩余空槽数 / Get the current free-slot count
   * @return 当前空槽个数 / Current number of free slots
   */
  size_t EmptySize() const { return core_.EmptySize(); }

  /**
   * @brief 获取队列最大容量 / Get the maximum queue capacity
   * @return 队列容量 / Queue capacity
   */
  size_t MaxSize() const { return core_.MaxSize(); }

 protected:
  SPSCQueueCore core_;  ///< 共享单生产者字节内核 / Shared single-producer byte core.

 private:
  /**
   * @brief 获取指定槽位中的 payload 指针 / Get the payload pointer stored in one slot
   * @param index 槽位下标 / Slot index
   */
  Data* SlotPtr(size_t index)
  {
    return std::launder(reinterpret_cast<Data*>(core_.PayloadPtr(index)));
  }

  /**
   * @brief 获取指定槽位中的 payload 指针（只读）
   *        / Get the payload pointer stored in one slot (const)
   * @param index 槽位下标 / Slot index
   */
  const Data* ConstSlotPtr(size_t index) const
  {
    return std::launder(reinterpret_cast<const Data*>(core_.PayloadPtr(index)));
  }
};
}  // namespace LibXR
