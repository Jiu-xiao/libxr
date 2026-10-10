#pragma once

#include <cstddef>
#include <type_traits>

#include "mpmc_queue_base.hpp"
#include "queue_typed_base.hpp"

namespace LibXR
{
/**
 * @class MPMCQueue
 * @brief 带固定 payload 类型的有界 MPMC 队列。
 *        Bounded MPMC queue with a fixed payload type.
 *
 * 队列内部只把 payload 当成原始字节块搬运，不会在内部形成 `Payload*` 指针，
 * 因此 `Payload` 的对齐要求不会额外约束队列内部字节缓冲区的语义边界。
 *
 * The queue moves payloads only as raw byte blocks and never forms internal
 * `Payload*` pointers, so `Payload` alignment does not add an extra semantic
 * constraint on the internal byte-buffer contract.
 *
 * 适用场合：MPMC 保留为公共类型，适合任务或线程之间、调用方能等待或稍后
 * 重试的场合，以及大容量、多核高争用的场合。单核上若某个操作在"占槽"与
 * "发布"之间被抢占，其他上下文会看到瞬时的 FULL/EMPTY（假的满/空），因此：
 * 被抢占时拿到的 FULL/EMPTY 可能是瞬时的，调用方要能等待或稍后重试；不要
 * 在中断里循环重试，也不要断言入队一定成功；RTOS 里高优先级任务拿到
 * FULL/EMPTY 时要让出 CPU 再试，不能原地自旋。中断会读写、拿到 FULL 只能
 * 丢数据的场合，改用 `SlotPool`。
 *
 * Intended use: MPMC stays a public type for task/thread producers and
 * consumers that can wait or retry later, and for large capacities with high
 * multi-core contention. On a single core, when one context is preempted
 * between claiming a cell and publishing it, other contexts observe transient
 * FULL/EMPTY (spurious full/empty). Therefore: a FULL/EMPTY observed while
 * someone is preempted may be transient and callers must be able to wait or
 * retry later; never retry in a loop inside an interrupt and never assert that
 * an enqueue must succeed; in an RTOS, a higher-priority task that observes
 * FULL/EMPTY must yield the CPU before retrying instead of spinning; and when
 * interrupts both read and write and a FULL can only drop data, use
 * `SlotPool` instead.
 *
 * @tparam Payload 队列存储的数据类型 / Queue element type.
 */
template <typename Payload>
class MPMCQueue final : public QueueTypedBase<MPMCQueue<Payload>, Payload>,
                        public MPMCQueueBase
{
  /// @brief 仅接受平凡可拷贝 payload / Accepts only trivially copyable payloads.
  static_assert(std::is_trivially_copyable_v<Payload>,
                "MPMCQueue requires trivially copyable payloads");
  /// @brief 仅接受平凡可析构 payload / Accepts only trivially destructible payloads.
  static_assert(std::is_trivially_destructible_v<Payload>,
                "MPMCQueue requires trivially destructible payloads");

 public:
  using ValueType = Payload;  ///< 队列元素类型 / Queue element type.
  /// @brief 重新公开强类型出队接口 / Re-expose the typed pop interface.
  using QueueTypedBase<MPMCQueue<Payload>, Payload>::Pop;
  /// @brief 重新公开强类型入队接口 / Re-expose the typed push interface.
  using QueueTypedBase<MPMCQueue<Payload>, Payload>::Push;

  /**
   * @brief 构造一个 MPMC 队列 / Construct one MPMC queue.
   * @param capacity 队列容量 / Queue capacity.
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation.
   */
  explicit MPMCQueue(size_t capacity) : MPMCQueueBase(sizeof(Payload), capacity) {}
};
}  // namespace LibXR
