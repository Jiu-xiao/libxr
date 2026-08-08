#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief 基于位置的 UART linked-list DMA RX 模型 / Position-based linked-list DMA RX
 * model for UART
 *
 * 后端将 descriptor ring 映射到所给连续存储的等长片段，并报告实时 producer 指针。
 * 模型只持有存储视图和上次消费的逻辑环位置；descriptor 对象和硬件状态仍归后端所有。
 * / The backend maps a descriptor ring onto equal fragments of contiguous storage and
 * reports one live producer pointer. The model owns only the storage view and last
 * consumed logical position; descriptors and hardware state remain backend-owned.
 *
 * 同一实例的调用不得重叠。调用者必须在通过 `OnDataAvailable()` 采样 producer 前取得
 * 完整 RX/config gate。软件队列无法容纳的字节会被丢弃，但位置仍向前推进。 / Calls
 * for one instance must not overlap. The caller must acquire the complete RX/config gate
 * before sampling the producer. Bytes that do not fit the software queue are dropped
 * while the position advances.
 *
 * @tparam DescriptorCount descriptor ring 中的等长片段数 / Number of equal fragments
 * in the descriptor ring
 */
template <size_t DescriptorCount = 4U>
class UartLinkedListDmaRxModel
{
 public:
  static_assert(DescriptorCount > 0U, "a linked-list RX ring needs a descriptor");

  /**
   * @brief 使用平台提供的 DMA 存储构造模型 / Construct with platform-provided DMA
   * storage
   *
   * 空存储禁用 linked-list RX；启用时必须为每个 descriptor 至少提供一个字节，并能
   * 整除为等长片段。 / Empty storage disables linked-list RX. Enabled storage must
   * contain at least one byte per descriptor and divide into equal fragments.
   *
   * @param storage DMA 可写存储；生命周期必须覆盖模型及 DMA 运行期 / DMA-writable
   * storage that must outlive the model and active DMA
   */
  explicit UartLinkedListDmaRxModel(RawData storage) : storage_(storage)
  {
    REQUIRE((storage_.size_ == 0U) ||
            ((storage_.addr_ != nullptr) && (storage_.size_ >= DescriptorCount) &&
             ((storage_.size_ % DescriptorCount) == 0U)));
  }

  /**
   * @brief 重置软件游标并启动后端 linked-list DMA ring / Reset the software cursor and
   * start the backend linked-list DMA ring
   * @tparam Backend 提供 linked-list RX hook 的平台后端 / Platform backend providing
   * linked-list RX hooks
   * @param backend 后端实例 / Backend instance
   */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartLinkedListDmaRx(Buffer(), BufferSize(), DescriptorCount);
  }

  /**
   * @brief 将上次 DMA 事件后产生的字节提交给 RX 队列 / Offer bytes produced since the
   * previous DMA event to the RX queue
   * @tparam Backend 提供 producer 采样和缓存维护 hook 的平台后端 / Platform backend
   * providing producer sampling and cache-maintenance hooks
   * @param backend 后端实例 / Backend instance
   * @param port 接收字节的读取端口 / Read port receiving produced bytes
   * @return 至少一个字节成功进入 RX 队列时为 true / True when at least one byte was
   * successfully enqueued
   *
   * 后端必须在 `GetLinkedListDmaRxProducer()` 中恰好采样一次实时 producer 指针。首字节
   * 和尾后地址都是合法环位置，尾后地址归一化为零；两次观察间跨过一个或多个完整环仍
   * 无法与未移动区分。 / The backend samples its live producer exactly once. Both the
   * first byte and one-past-end address are valid positions, with one-past-end normalized
   * to zero. One or more complete wraps remain indistinguishable from no movement.
   *
   * @pre 距上次成功采样后产生的字节数必须严格小于 `BufferSize()` / Bytes produced
   * since the previous successful sample must be strictly less than `BufferSize()`
   *
   * 调用者在释放 RX/config 硬件 gate 后完成挂起读取。本方法只采样 DMA 状态、复制字节
   * 并推进 SPSC producer。 / The caller completes pending reads after releasing its
   * RX/config hardware gate. This method only samples DMA state, copies bytes, and
   * advances the SPSC producer.
   */
  template <typename Backend>
  [[nodiscard]] bool OnDataAvailable(Backend& backend, ReadPort& port)
  {
    uint8_t* const buffer = Buffer();
    const size_t capacity = BufferSize();
    if (capacity == 0U)
    {
      return false;
    }

    uint8_t* const producer = backend.GetLinkedListDmaRxProducer();
    const uintptr_t buffer_address = reinterpret_cast<uintptr_t>(buffer);
    const uintptr_t producer_address = reinterpret_cast<uintptr_t>(producer);
    if (producer_address < buffer_address)
    {
      ASSERT(false);
      return false;
    }

    size_t current_position = producer_address - buffer_address;
    if (current_position > capacity)
    {
      ASSERT(false);
      return false;
    }
    if (current_position == capacity)
    {
      current_position = 0U;
    }

    if (current_position == last_position_)
    {
      return false;
    }

    bool pushed_any = false;
    if (current_position > last_position_)
    {
      backend.PrepareLinkedListDmaRxForCpu(&buffer[last_position_],
                                           current_position - last_position_);
      pushed_any =
          port.queue_data_->PushBatch(&buffer[last_position_],
                                      current_position - last_position_) == ErrorCode::OK;
    }
    else
    {
      backend.PrepareLinkedListDmaRxForCpu(&buffer[last_position_],
                                           capacity - last_position_);
      if (port.queue_data_->PushBatch(&buffer[last_position_],
                                      capacity - last_position_) == ErrorCode::OK)
      {
        pushed_any = true;
      }
      if (current_position != 0U)
      {
        backend.PrepareLinkedListDmaRxForCpu(buffer, current_position);
        if (port.queue_data_->PushBatch(buffer, current_position) == ErrorCode::OK)
        {
          pushed_any = true;
        }
      }
    }

    last_position_ = current_position;
    return pushed_any;
  }

  /** @brief 将软件游标重置到逻辑环起点 / Reset the software cursor. */
  void ResetPosition() { last_position_ = 0U; }

  /** @return DMA 可写缓冲区地址 / DMA-writable buffer address. */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /** @return 逻辑环总字节容量 / Logical ring capacity in bytes. */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /** @return 上次消费的逻辑 producer 偏移 / Last consumed logical producer offset. */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
