#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief Position-based circular DMA RX model for UART.
 *
 * The model owns the DMA storage view and the last consumed write position. The backend
 * starts circular DMA, reports its remaining count, and performs cache maintenance.
 * Bytes that do not fit in the software queue are dropped while the position still
 * advances, preserving the existing UART overrun behavior.
 *
 * Calls for one instance must not overlap. Related UART and RX DMA IRQs must use the
 * same preemption priority and target-core affinity. CONFIG must exclude the hardware
 * fragment around `OnDataAvailable()` with the backend RX/config gate.
 */
class UartCircularDmaRxModel
{
 public:
  /**
   * @brief Construct the model with platform-provided DMA storage.
   *
   * Empty storage disables circular RX. Enabled storage needs at least two bytes so
   * the modulo DMA position has more than one observable value.
   */
  explicit UartCircularDmaRxModel(RawData storage) : storage_(storage)
  {
    REQUIRE((storage_.size_ == 0U) ||
            ((storage_.addr_ != nullptr) && (storage_.size_ >= 2U)));
  }

  /** Reset the software cursor and start the backend circular DMA channel. */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartCircularDmaRx(Buffer(), BufferSize());
  }

  /**
   * @brief Offer bytes produced since the previous DMA event to the RX queue.
   * @return true when the DMA position advanced.
   *
   * The caller completes pending reads after releasing its RX/config hardware gate.
   * This method only reads DMA state, copies bytes, and advances the SPSC producer.
   */
  template <typename Backend>
  [[nodiscard]] bool OnDataAvailable(Backend& backend, ReadPort& port)
  {
    uint8_t* const buffer = Buffer();
    const size_t capacity = BufferSize();
    const size_t remaining = backend.GetCircularDmaRxRemaining();
    if (remaining > capacity)
    {
      ASSERT(false);
      return false;
    }

    const size_t current_position = capacity - remaining;

    if (current_position == last_position_)
    {
      return false;
    }

    if (current_position > last_position_)
    {
      backend.PrepareCircularDmaRxForCpu(&buffer[last_position_],
                                         current_position - last_position_);
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        current_position - last_position_);
    }
    else
    {
      backend.PrepareCircularDmaRxForCpu(&buffer[last_position_],
                                         capacity - last_position_);
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        capacity - last_position_);
      if (current_position != 0U)
      {
        backend.PrepareCircularDmaRxForCpu(buffer, current_position);
        (void)port.queue_data_->PushBatch(buffer, current_position);
      }
    }

    last_position_ = current_position;
    return true;
  }

  /** Reset the software cursor to the beginning of the DMA buffer. */
  void ResetPosition() { last_position_ = 0U; }

  /** Return the DMA-writable buffer address. */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /** Return the DMA buffer capacity in bytes. */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /** Return the last consumed DMA write offset. */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
