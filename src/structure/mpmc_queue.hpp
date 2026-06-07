#pragma once

#include <cstddef>
#include <type_traits>

#include "mpmc_queue_core.hpp"

namespace LibXR
{
/**
 * @class MPMCQueue
 * @brief 带固定 payload 类型的有界 MPMC 队列 / Bounded MPMC queue with a fixed payload type
 */
template <typename Payload>
class MPMCQueue
{
  static_assert(std::is_trivially_copyable_v<Payload>,
                "MPMCQueue requires trivially copyable payloads");
  static_assert(std::is_trivially_destructible_v<Payload>,
                "MPMCQueue requires trivially destructible payloads");
  static_assert(alignof(Payload) <= alignof(size_t),
                "MPMCQueue only supports payloads aligned no stricter than size_t");

 public:
  using ValueType = Payload;

  /**
   * @brief 构造一个 MPMC 队列 / Construct one MPMC queue
   * @param capacity 队列容量 / Queue capacity
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  explicit MPMCQueue(size_t capacity)
      : core_(sizeof(Payload), capacity)
  {
  }

  /**
   * @brief 向队列尾部推入一个 payload / Push one payload into the queue tail
   * @param value 待入队 payload / Payload to enqueue
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  ErrorCode Push(const Payload& value) { return core_.PushBytes(&value); }

  /**
   * @brief 从队列头部弹出一个 payload / Pop one payload from the queue head
   * @param value 用于接收 payload / Receives the dequeued payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop(Payload& value) { return core_.PopBytes(&value); }

  /**
   * @brief 丢弃一个队头 payload / Discard one front payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop() { return core_.PopBytes(); }

  /**
   * @brief 获取队列最大容量 / Get the maximum queue capacity
   * @return 队列容量 / Queue capacity
   */
  [[nodiscard]] size_t MaxSize() const { return core_.MaxSize(); }

  /**
   * @brief 获取当前已用元素数 / Get the current element count
   * @return 当前元素个数 / Current number of stored payloads
   */
  [[nodiscard]] size_t Size() const { return core_.Size(); }

  /**
   * @brief 获取剩余空槽数 / Get the current free-slot count
   * @return 当前空槽个数 / Current number of free slots
   */
  [[nodiscard]] size_t EmptySize() const { return core_.EmptySize(); }

 protected:
  MPMCQueueCore core_;
};
}  // namespace LibXR
