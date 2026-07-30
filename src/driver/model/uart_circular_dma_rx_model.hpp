#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief UART 基于位置的循环 DMA 接收模型 / Position-based circular DMA RX model for UART
 *
 * 模型管理 DMA 存储区视图和软件读取位置。平台后端负责启动循环 DMA、返回剩余传输计数，
 * 并在 CPU 读取前执行所需的缓存维护。
 * The model owns the DMA storage view and software read position. The platform backend
 * starts circular DMA, reports the remaining transfer count, and performs any cache
 * maintenance required before CPU access.
 *
 * 为保持现有 UART 行为，软件队列写满时仍推进读取位置，无法写入的数据会被丢弃。
 * To preserve existing UART behavior, the read position advances when the software queue
 * is full, and bytes that cannot be queued are dropped.
 *
 * @warning 同一模型实例的 RX 事件入口不得重入。若 UART IDLE 与 RX DMA HT/TC 使用
 * 不同中断源，平台驱动必须把这些中断配置为相同的抢占优先级，保证任意时刻只有一个
 * `OnDataAvailable()` 修改读取位置并作为软件队列 producer。
 * Calls delivering RX events to one model instance must not overlap. When UART IDLE and
 * RX DMA HT/TC use different interrupt sources, the platform driver must configure them
 * with the same preemption priority and target-core affinity so only one
 * `OnDataAvailable()` call can modify the read position and act as the software-queue
 * producer at a time. Configuration on another core must use a separate hardware-state
 * handoff such as `UartRxConfigGate`.
 */
class UartCircularDmaRxModel
{
 public:
  /**
   * @brief 使用平台提供的 DMA 存储区构造接收模型 / Construct the RX model with
   * platform-provided DMA storage
   * @param storage DMA 可写的循环接收缓冲区 / DMA-writable circular receive buffer
   */
  explicit UartCircularDmaRxModel(RawData storage) : storage_(storage) {}

  /**
   * @brief 复位读取位置并启动循环 DMA / Reset the read position and start circular DMA
   * @tparam Backend 静态绑定的平台后端类型 / Statically bound platform backend type
   * @param backend 提供 `StartCircularDmaRx()` 的平台后端 / Platform backend providing
   * `StartCircularDmaRx()`
   */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartCircularDmaRx(Buffer(), BufferSize());
  }

  /**
   * @brief 消费上次 DMA 事件后新产生的数据 / Consume bytes produced since the previous
   * DMA event
   * @tparam Backend 静态绑定的平台后端类型 / Statically bound platform backend type
   * @param backend 提供剩余计数和缓存维护操作的平台后端 / Platform backend providing
   * remaining-count and cache-maintenance operations
   * @param port 接收新增数据的读端口 / Read port receiving newly produced bytes
   * @param in_isr 是否在中断上下文完成 pending 读取 / Whether pending reads are completed
   * in interrupt context
   * @warning 调用方必须保证同一模型实例上的调用不重入；相关 UART 与 RX DMA IRQ 必须
   * 使用相同抢占优先级。Calls for the same model instance must not overlap; related
   * UART and RX DMA IRQs must use the same preemption priority.
   */
  template <typename Backend>
  void OnDataAvailable(Backend& backend, ReadPort& port, bool in_isr)
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
    backend.PrepareCircularDmaRxForCpu(buffer, capacity);

    if (current_position == last_position_)
    {
      return;
    }

    if (current_position > last_position_)
    {
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        current_position - last_position_);
    }
    else
    {
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        capacity - last_position_);
      (void)port.queue_data_->PushBatch(buffer, current_position);
    }

    last_position_ = current_position;
    port.ProcessPendingReads(in_isr);
  }

  /**
   * @brief 将软件读取位置复位到 DMA 缓冲区起点 / Reset the software read position to the
   * start of the DMA buffer
   */
  void ResetPosition() { last_position_ = 0U; }

  /**
   * @brief 获取 DMA 可写缓冲区起始地址 / Get the DMA-writable buffer start address
   * @return DMA 缓冲区起始地址 / DMA buffer start address
   */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /**
   * @brief 获取循环 DMA 缓冲区容量 / Get the circular DMA buffer capacity
   * @return 缓冲区字节数 / Buffer capacity in bytes
   */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /**
   * @brief 获取上次已消费的 DMA 写入位置 / Get the last consumed DMA write position
   * @return 相对缓冲区起点的字节偏移 / Byte offset from the start of the buffer
   */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
