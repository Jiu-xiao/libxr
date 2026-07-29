#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#if defined(STM32H5) && defined(HAL_UART_MODULE_ENABLED) && !defined(STM32H503xx)
#error "LibXR STM32 H5 GPDMA UART support is currently validated only for STM32H503"
#endif

#if defined(STM32H5) && defined(HAL_UART_MODULE_ENABLED)

#if !defined(HAL_DMA_MODULE_ENABLED)
#error "LibXR STM32H503 GPDMA UART support requires the HAL DMA module"
#endif

#if !defined(USART_DMAREQUESTS_SW_WA)
#error "STM32H503 HAL must preserve UART DMA request bits for ES0561 section 2.11.2"
#endif

namespace LibXR
{

/**
 * @brief STM32H503 GPDMA details used by the common UART DMA model.
 *
 * The adapter owns a private four-node linear-addressing RX queue but not its payload
 * storage. It replaces the CubeMX seed queue at the first stopped RX start, samples the
 * live producer only through `GetLinkedListDmaRxProducer()`, and joins asynchronous
 * per-channel aborts without changing HAL handle state. UART `DMAT` and `DMAR` remain
 * untouched as required by STM32H503 erratum ES0561 section 2.11.2.
 */
class STM32H5GpdmaUartAdapter
{
 public:
  static constexpr size_t RX_NODE_COUNT = 4U;

  using AbortCallback = void (*)(DMA_HandleTypeDef* dma_handle);

  explicit STM32H5GpdmaUartAdapter(UART_HandleTypeDef* uart_handle);

  /**
   * @brief Build, attach, and start the private circular linked-list RX queue.
   *
   * `total_size` is split into four equal descriptor blocks. On later calls the same
   * queue and payload must be supplied. The returned HAL status preserves the UART
   * line-error case for the owning backend to classify as a deferred recovery carrier.
   */
  HAL_StatusTypeDef StartLinkedListDmaRx(uint8_t* buffer, size_t total_size,
                                         size_t descriptor_count, bool in_isr);

  /** Read the live GPDMA destination address exactly once. */
  [[nodiscard]] uint8_t* GetLinkedListDmaRxProducer() const;

  /** Make one DMA-written RX span visible to the CPU. */
  static void PrepareLinkedListDmaRxForCpu(uint8_t* data, size_t size);

  /** Disable only UART TC notification; preserve H503 DMAT and DMAR request bits. */
  void CloseTxTerminalSource() const;

  /**
   * @brief Start or join one non-blocking GPDMA channel abort.
   *
   * A BUSY channel requires its NVIC vector to be enabled on entry. The vector is
   * masked only across HAL's BUSY-to-ABORT publication and restored to its exact prior
   * enable state without clearing pending status.
   */
  [[nodiscard]] bool LaunchStop(DMA_HandleTypeDef* dma_handle, AbortCallback callback,
                                bool in_isr);

  /** Return true only after HAL and the GPDMA channel both reached the stopped state. */
  [[nodiscard]] static bool StopComplete(DMA_HandleTypeDef* dma_handle);

  /** Return true when both UART DMA directions are stopped or absent. */
  [[nodiscard]] bool AllStopsComplete() const;

  /**
   * @brief Capture stopped normal-TX evidence without inferring completion from CBR1.
   *
   * H503 silicon readback after asynchronous channel RESET is not yet hardware-proven.
   * Therefore this adapter records errors but conservatively leaves `payload_complete`
   * false so an unretired active record is replayed from byte zero.
   */
  void CaptureStoppedTx(DMA_HandleTypeDef* dma_handle, bool& evidence_captured,
                        bool& payload_complete, bool& error) const;

  /** Verify stop postconditions, reset linked-list progress, and clear IRQ sources. */
  static void FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr);

 private:
  // Keeping all state in the descriptor-aligned block avoids rounding the adapter up
  // to multiple 256-byte allocation units while retaining the GPDMA address-window
  // guarantee for the four nodes at the beginning of the block.
  struct alignas(256) StateBlock
  {
    DMA_NodeTypeDef nodes[RX_NODE_COUNT];
    DMA_QListTypeDef rx_queue{};
    UART_HandleTypeDef* uart_handle = nullptr;
    uint8_t* rx_buffer = nullptr;
    size_t rx_total_size = 0U;
    size_t rx_node_size = 0U;
    bool rx_queue_built = false;
  };

  static_assert(offsetof(StateBlock, nodes) == 0U);
  static_assert(sizeof(StateBlock) == 256U);

  [[nodiscard]] static bool IsStopped(DMA_HandleTypeDef* dma_handle);
  [[nodiscard]] static bool HasTransferError(DMA_HandleTypeDef* dma_handle);
  static void DisableInterrupts(DMA_HandleTypeDef* dma_handle);
  static void ClearFlags(DMA_HandleTypeDef* dma_handle);

  void BuildRxQueue(uint8_t* buffer, size_t total_size, size_t descriptor_count,
                    bool in_isr);

  StateBlock state_{};
};

static_assert(sizeof(STM32H5GpdmaUartAdapter) == 256U);

}  // namespace LibXR

#endif
