#pragma once

#include <cstddef>

#include "queue_typed_base.hpp"
#include "spmc_queue_base.hpp"

namespace LibXR
{
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
