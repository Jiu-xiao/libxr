#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#if defined(STM32H5) || defined(STM32U5) || defined(STM32U3) || defined(STM32N6) || \
    defined(STM32H7RS)
#define LIBXR_STM32_ADC_GPDMA 1
#endif

#if defined(LIBXR_STM32_ADC_GPDMA) && defined(HAL_ADC_MODULE_ENABLED)

#if !defined(HAL_DMA_MODULE_ENABLED) || !defined(DMA_LINKEDLIST_CIRCULAR) || \
    !defined(DMA_GPDMA_LINEAR_NODE) || !defined(IS_GPDMA_INSTANCE)
#error "LibXR STM32 GPDMA ADC support requires the HAL linked-list GPDMA API"
#endif

namespace LibXR
{

/**
 * @brief Private linear one-node GPDMA queue used by continuous ADC sampling.
 *
 * CubeMX supplies the request, widths, ports, and priority through its seed node. The
 * adapter copies those fields but never inherits its node type or 2D repeat-address
 * fields. Only GPDMA is accepted; LPDMA and HPDMA need controller-specific adapters.
 */
class STM32GpdmaAdcAdapter
{
 public:
  STM32GpdmaAdcAdapter() = default;

  /** Attach the private circular queue and start continuous ADC DMA. */
  HAL_StatusTypeDef Start(ADC_HandleTypeDef* adc_handle, uint32_t* buffer,
                          uint32_t sample_count, size_t buffer_size);

  /** Stop ADC DMA and restore the CubeMX-owned seed queue. */
  HAL_StatusTypeDef Stop(ADC_HandleTypeDef* adc_handle);

 private:
  struct alignas(32) StateBlock
  {
    DMA_NodeTypeDef node{};
    DMA_QListTypeDef queue{};
    DMA_QListTypeDef* original_queue = nullptr;
  };

  static_assert(sizeof(StateBlock) == 64U);

  void BuildAndAttach(ADC_HandleTypeDef* adc_handle, uint32_t* buffer,
                      uint32_t sample_count, size_t buffer_size);

  StateBlock state_{};
};

static_assert(sizeof(STM32GpdmaAdcAdapter) == 64U);

}  // namespace LibXR

#endif
