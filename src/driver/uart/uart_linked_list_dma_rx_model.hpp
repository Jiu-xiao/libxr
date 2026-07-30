#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief Position-based linked-list DMA RX model for UART.
 *
 * The backend maps a descriptor ring onto contiguous, equally sized fragments of the
 * supplied storage and reports one live producer pointer. The model owns only that
 * storage view and the last consumed logical ring position; descriptor objects and
 * hardware state remain backend-owned.
 *
 * Calls for one instance must not overlap. The caller must acquire its complete
 * RX/config gate before sampling the producer through `OnDataAvailable()`. Bytes that
 * do not fit in the software queue are dropped while the position still advances,
 * preserving the existing UART overrun behavior.
 *
 * @tparam DescriptorCount Number of equally sized fragments in the descriptor ring.
 */
template <size_t DescriptorCount = 4U>
class UartLinkedListDmaRxModel
{
 public:
  static_assert(DescriptorCount > 0U, "a linked-list RX ring needs a descriptor");

  /**
   * @brief Construct the model with platform-provided DMA storage.
   *
   * Empty storage disables linked-list RX. Enabled storage must contain at least one
   * byte per descriptor and divide exactly into equal descriptor fragments.
   */
  explicit UartLinkedListDmaRxModel(RawData storage) : storage_(storage)
  {
    REQUIRE((storage_.size_ == 0U) ||
            ((storage_.addr_ != nullptr) && (storage_.size_ >= DescriptorCount) &&
             ((storage_.size_ % DescriptorCount) == 0U)));
  }

  /** Reset the software cursor and start the backend linked-list DMA ring. */
  template <typename Backend>
  void Start(Backend& backend)
  {
    ResetPosition();
    backend.StartLinkedListDmaRx(Buffer(), BufferSize(), DescriptorCount);
  }

  /**
   * @brief Offer bytes produced since the previous DMA event to the RX queue.
   * @return true when the logical producer position advanced.
   *
   * The backend must sample its live producer pointer exactly once in
   * `GetLinkedListDmaRxProducer()`. Both the first byte and the one-past-end address are
   * valid ring positions; the latter is normalized to offset zero. Movement by one or
   * more complete rings between observations remains indistinguishable from no movement.
   *
   * The caller completes pending reads after releasing its RX/config hardware gate.
   * This method only samples DMA state, copies bytes, and advances the SPSC producer.
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

    if (current_position > last_position_)
    {
      backend.PrepareLinkedListDmaRxForCpu(&buffer[last_position_],
                                           current_position - last_position_);
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        current_position - last_position_);
    }
    else
    {
      backend.PrepareLinkedListDmaRxForCpu(&buffer[last_position_],
                                           capacity - last_position_);
      (void)port.queue_data_->PushBatch(&buffer[last_position_],
                                        capacity - last_position_);
      if (current_position != 0U)
      {
        backend.PrepareLinkedListDmaRxForCpu(buffer, current_position);
        (void)port.queue_data_->PushBatch(buffer, current_position);
      }
    }

    last_position_ = current_position;
    return true;
  }

  /** Reset the software cursor to the beginning of the logical ring. */
  void ResetPosition() { last_position_ = 0U; }

  /** Return the DMA-writable buffer address. */
  [[nodiscard]] uint8_t* Buffer() const { return static_cast<uint8_t*>(storage_.addr_); }

  /** Return the total logical ring capacity in bytes. */
  [[nodiscard]] size_t BufferSize() const { return storage_.size_; }

  /** Return the last consumed logical producer offset. */
  [[nodiscard]] size_t LastPosition() const { return last_position_; }

 private:
  RawData storage_;
  size_t last_position_ = 0U;
};

}  // namespace LibXR
