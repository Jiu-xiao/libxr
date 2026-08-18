#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief 基于位置的 UART 循环 DMA RX 模型 / Position-based circular DMA RX model for
 * UART
 *
 * 模型持有 DMA 存储视图和上次消费位置；后端负责启动循环 DMA、报告剩余计数并执行缓存
 * 维护。软件队列无法容纳的字节会被丢弃，但消费位置仍向前推进，以保持 UART overrun
 * 语义。 / The model owns the DMA storage view and last consumed position. The backend
 * starts circular DMA, reports the remaining count, and performs cache maintenance.
 * Bytes that do not fit the software queue are dropped while the position advances.
 *
 * 模型只保留模缓冲区位置，因此两次成功采样之间产生的字节数必须严格小于 DMA 缓冲区
 * 容量。恰好一整圈或多整圈与“没有移动”不可区分；HT/TC 只能作为采样唤醒，不能作为
 * generation 计数，且事件可能合并。 / The model retains only a position modulo the
 * buffer capacity, so fewer than one buffer capacity of bytes may be produced between two
 * successful samples. One or more complete unseen wraps alias no movement. HT/TC events
 * are sampling wakeups, not generation counters, and may coalesce.
 *
 * 同一实例的调用不得重叠。相关 UART 和 RX DMA IRQ 必须具有相同抢占优先级和目标核；
 * CONFIG 必须通过后端 RX/config gate 排除 `OnDataAvailable()` 周围的硬件片段。 / Calls
 * for one instance must not overlap. Related UART and RX DMA IRQs must share preemption
 * priority and target-core affinity. CONFIG must exclude the hardware fragment around
 * `OnDataAvailable()` through the backend RX/config gate.
 */
class UartCircularDmaRxModel
{
 public:
  /**
   * @brief 使用平台提供的 DMA 存储构造模型 / Construct with platform-provided DMA
   * storage
   *
   * 空存储禁用循环 RX；启用时至少需要两个字节，使模运算后的 DMA 位置具有多个可观察值。
   * / Empty storage disables circular RX. Enabled storage needs at least two bytes so
   * the modulo DMA position has more than one observable value.
   *
   * @param storage DMA 可写存储；生命周期必须覆盖模型及 DMA 运行期 / DMA-writable
   * storage that must outlive the model and active DMA
   */
  explicit UartCircularDmaRxModel(RawData storage) : storage_(storage)
  {
    REQUIRE((storage_.size_ == 0U) ||
            ((storage_.addr_ != nullptr) && (storage_.size_ >= 2U)));
  }

  /**
   * @brief 重置软件游标并启动后端循环 DMA / Reset the software cursor and start the
   * backend circular DMA
   * @tparam Backend 提供循环 RX hook 的平台后端 / Platform backend providing circular
   * RX hooks
   * @param backend 后端实例 / Backend instance
   */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartCircularDmaRx(Buffer(), BufferSize());
  }

  /**
   * @brief 将上次 DMA 事件后产生的字节提交给 RX 队列 / Offer bytes produced since the
   * previous DMA event to the RX queue
   * @tparam Backend 提供位置读取和缓存维护 hook 的平台后端 / Platform backend
   * providing position and cache-maintenance hooks
   * @param backend 后端实例 / Backend instance
   * @param queue 调用者持有的 RX producer scope / Caller-owned RX producer scope
   *
   * @pre 距上次成功采样后产生的字节数必须严格小于 `BufferSize()` / Bytes produced
   * since the previous successful sample must be strictly less than `BufferSize()`
   *
   * 调用者在释放 RX/config 硬件 gate 后调用 `queue.Publish()`。本方法只读 DMA 状态、
   * 复制字节并推进 SPSC producer。 / The caller invokes `queue.Publish()` after
   * releasing its RX/config hardware gate. This method only reads DMA state, copies
   * bytes, and advances the SPSC producer.
   */
  template <typename Backend>
  void OnDataAvailable(Backend& backend, ReadPort::ReadQueue& queue)
  {
    uint8_t* const buffer = Buffer();
    const size_t capacity = BufferSize();
    const size_t remaining = backend.GetCircularDmaRxRemaining();
    if (remaining > capacity)
    {
      ASSERT(false);
      return;
    }

    const size_t current_position = capacity - remaining;

    if (current_position == last_position_)
    {
      return;
    }

    if (current_position > last_position_)
    {
      backend.PrepareCircularDmaRxForCpu(&buffer[last_position_],
                                         current_position - last_position_);
      (void)queue.PushBatch(&buffer[last_position_], current_position - last_position_);
    }
    else
    {
      backend.PrepareCircularDmaRxForCpu(&buffer[last_position_],
                                         capacity - last_position_);
      (void)queue.PushBatch(&buffer[last_position_], capacity - last_position_);
      if (current_position != 0U)
      {
        backend.PrepareCircularDmaRxForCpu(buffer, current_position);
        (void)queue.PushBatch(buffer, current_position);
      }
    }

    last_position_ = current_position;
  }

  /** @brief 将软件游标重置到 DMA 缓冲区起点 / Reset the software cursor. */
  void ResetPosition() { last_position_ = 0U; }

  /** @return DMA 可写缓冲区地址 / DMA-writable buffer address. */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /** @return DMA 缓冲区字节容量 / DMA buffer capacity in bytes. */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /** @return 上次消费的 DMA 写偏移 / Last consumed DMA write offset. */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
