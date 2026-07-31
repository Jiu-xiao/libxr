#pragma once

#include <cstddef>
#include <cstdint>

#include "main.h"

#if (defined(STM32H5) || defined(STM32U5) || defined(STM32U3) || defined(STM32N6) || \
     defined(STM32H7RS)) &&                                                          \
    defined(HAL_ADC_MODULE_ENABLED) && defined(HAL_DMA_MODULE_ENABLED)

#if !defined(DMA_LINKEDLIST_CIRCULAR) || !defined(DMA_GPDMA_LINEAR_NODE) ||          \
    !defined(IS_GPDMA_INSTANCE) || !defined(IS_DMA_BLOCK_SIZE) ||                    \
    !defined(DMA_LSM_FULL_EXECUTION) || !defined(DMA_CCR_LSM) ||                     \
    !defined(DMA_BREQ_SINGLE_BURST) || !defined(DMA_EXCHANGE_NONE) ||                \
    !defined(DMA_TRIG_POLARITY_MASKED) || !defined(DMA_PERIPH_TO_MEMORY) ||          \
    !defined(DMA_SINC_FIXED) || !defined(DMA_DINC_INCREMENTED) ||                    \
    !defined(DMA_SRC_DATAWIDTH_HALFWORD) || !defined(DMA_DEST_DATAWIDTH_HALFWORD) || \
    !defined(DMA_NORMAL)
#error "LibXR STM32 GPDMA ADC support requires the HAL linked-list GPDMA API"
#endif

#define LIBXR_STM32_ADC_GPDMA 1
#endif

#if defined(LIBXR_STM32_ADC_GPDMA)

namespace LibXR
{

/**
 * @brief 连续 ADC 采样使用的私有单节点线性 GPDMA queue / Private linear one-node
 * GPDMA queue used by continuous ADC sampling
 *
 * CubeMX seed node 提供 request 与端口选择；adapter 校验采样传输几何并重建私有循环
 * queue，不继承 2D repeat-address 状态。仅接受 GPDMA，LPDMA 与 HPDMA 需要各自的
 * controller adapter。 / The CubeMX seed node supplies request and port selection. The
 * adapter validates the sampling geometry and rebuilds a private circular queue without
 * inheriting 2D repeat-address state. Only GPDMA is accepted; LPDMA and HPDMA require
 * controller-specific adapters. The adapter is started once and remains attached for the
 * ADC object's lifetime; runtime detach is not supported.
 */
class STM32GpdmaAdcAdapter
{
 public:
  STM32GpdmaAdcAdapter() = default;

  /**
   * @brief 挂接私有循环 queue 并启动连续 ADC DMA / Attach the private circular queue
   * and start continuous ADC DMA
   * @param adc_handle 已关联 CubeMX seed queue 的 ADC handle / ADC handle linked to a
   * CubeMX seed queue
   * @param buffer DMA 写入的半字采样存储 / Halfword sample storage written by DMA
   * @param sample_count 每轮循环的半字采样数 / Halfword sample count per cycle
   * @param buffer_size `buffer` 的可用字节数 / Available byte count in `buffer`
   * @return HAL ADC DMA 启动结果 / HAL ADC DMA start result
   */
  HAL_StatusTypeDef Start(ADC_HandleTypeDef* adc_handle, uint32_t* buffer,
                          uint32_t sample_count, size_t buffer_size);

 private:
  struct alignas(32) StateBlock
  {
    DMA_NodeTypeDef node{};
    DMA_QListTypeDef queue{};
  };

  static_assert((sizeof(StateBlock) % 32U) == 0U);

  void BuildAndAttach(ADC_HandleTypeDef* adc_handle, uint32_t* buffer,
                      uint32_t sample_count, size_t buffer_size);

  StateBlock state_{};
};

static_assert((sizeof(STM32GpdmaAdcAdapter) % 32U) == 0U);

}  // namespace LibXR

#endif
