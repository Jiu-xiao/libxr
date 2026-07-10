#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief Position-based circular DMA receive model for UART drivers.
 *
 * The model owns the DMA storage view and software read cursor. A platform
 * backend starts circular DMA, exposes the remaining transfer count, and
 * performs any cache maintenance required before the CPU reads DMA data.
 *
 * Queue overflow intentionally keeps the existing UART behavior: failed
 * `PushBatch()` calls are ignored and the DMA cursor still advances.
 */
class UartCircularDmaRxModel
{
 public:
  /**
   * @brief Construct a receive model over platform-provided DMA storage.
   * @param storage Writable circular DMA receive buffer.
   */
  explicit UartCircularDmaRxModel(RawData storage) : storage_(storage) {}

  /**
   * @brief Reset the cursor and start circular DMA through a platform backend.
   * @tparam Backend Statically bound platform backend type.
   * @param backend Backend providing `StartCircularDmaRx()`.
   */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartCircularDmaRx(Buffer(), BufferSize());
  }

  /**
   * @brief Consume bytes produced since the previous DMA event.
   * @tparam Backend Statically bound platform backend type.
   * @param backend Backend providing remaining-count and cache hooks.
   * @param port Read port receiving newly produced bytes.
   * @param in_isr Whether pending reads are completed from interrupt context.
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
   * @brief Reset the software cursor to the beginning of the DMA buffer.
   */
  void ResetPosition() { last_position_ = 0U; }

  /**
   * @brief Return the writable DMA buffer start address.
   */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /**
   * @brief Return the circular DMA buffer capacity in bytes.
   */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /**
   * @brief Return the last consumed DMA write position.
   */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
