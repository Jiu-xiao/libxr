#include "stm32_adc_gpdma.hpp"

#if defined(LIBXR_STM32_ADC_GPDMA) && defined(HAL_ADC_MODULE_ENABLED)

#include <limits>

#include "libxr_def.hpp"
#include "stm32_dcache.hpp"

namespace LibXR
{

HAL_StatusTypeDef STM32GpdmaAdcAdapter::Start(ADC_HandleTypeDef* adc_handle,
                                              uint32_t* buffer, uint32_t sample_count,
                                              size_t buffer_size)
{
  REQUIRE(adc_handle != nullptr);
  REQUIRE(adc_handle->DMA_Handle != nullptr);
  REQUIRE(buffer != nullptr);
  REQUIRE(sample_count > 0U);
  REQUIRE(sample_count <= std::numeric_limits<size_t>::max() / sizeof(uint16_t));
  REQUIRE(buffer_size >= static_cast<size_t>(sample_count) * sizeof(uint16_t));
  DMA_HandleTypeDef* const dma_handle = adc_handle->DMA_Handle;
  REQUIRE(dma_handle->Parent == adc_handle);
  REQUIRE(IS_GPDMA_INSTANCE(dma_handle->Instance) != 0U);
  REQUIRE(dma_handle->Mode == DMA_LINKEDLIST_CIRCULAR);
  REQUIRE(dma_handle->InitLinkedList.LinkedListMode == DMA_LINKEDLIST_CIRCULAR);
  REQUIRE(dma_handle->InitLinkedList.LinkStepMode == DMA_LSM_FULL_EXECUTION);
  REQUIRE((dma_handle->Instance->CCR & DMA_CCR_LSM) == DMA_LSM_FULL_EXECUTION);

  REQUIRE(dma_handle->LinkedListQueue != &state_.queue);
  BuildAndAttach(adc_handle, buffer, sample_count, buffer_size);

  STM32_CleanDCacheByAddr(&state_, sizeof(state_));
  STM32_CleanDCacheByAddr(buffer, buffer_size);
  STM32_InvalidateDCacheByAddr(buffer, buffer_size);

  return HAL_ADC_Start_DMA(adc_handle, buffer, sample_count);
}

void STM32GpdmaAdcAdapter::BuildAndAttach(ADC_HandleTypeDef* adc_handle, uint32_t* buffer,
                                          uint32_t sample_count, size_t buffer_size)
{
  DMA_HandleTypeDef* const dma_handle = adc_handle->DMA_Handle;
  REQUIRE(dma_handle->State == HAL_DMA_STATE_READY);
  REQUIRE((dma_handle->Instance->CCR & DMA_CCR_EN) == 0U);
  REQUIRE(dma_handle->LinkedListQueue != nullptr);
  REQUIRE(dma_handle->LinkedListQueue->Head != nullptr);
  REQUIRE(dma_handle->LinkedListQueue->FirstCircularNode != nullptr);
  REQUIRE(dma_handle->LinkedListQueue->State == HAL_DMA_QUEUE_STATE_READY);

  DMA_NodeConfTypeDef node_config{};
  REQUIRE(HAL_DMAEx_List_GetNodeConfig(&node_config, dma_handle->LinkedListQueue->Head) ==
          HAL_OK);
  REQUIRE(node_config.Init.Direction == DMA_PERIPH_TO_MEMORY);
  REQUIRE(node_config.Init.SrcInc == DMA_SINC_FIXED);
  REQUIRE(node_config.Init.DestInc == DMA_DINC_INCREMENTED);
  REQUIRE(node_config.Init.SrcDataWidth == DMA_SRC_DATAWIDTH_HALFWORD);
  REQUIRE(node_config.Init.DestDataWidth == DMA_DEST_DATAWIDTH_HALFWORD);
  REQUIRE(node_config.Init.SrcBurstLength == 1U);
  REQUIRE(node_config.Init.DestBurstLength == 1U);
  REQUIRE(node_config.Init.BlkHWRequest == DMA_BREQ_SINGLE_BURST);
  REQUIRE(node_config.Init.Mode == DMA_NORMAL);
  REQUIRE(node_config.TriggerConfig.TriggerPolarity == DMA_TRIG_POLARITY_MASKED);
  REQUIRE(node_config.DataHandlingConfig.DataExchange == DMA_EXCHANGE_NONE);
  REQUIRE(sample_count <= std::numeric_limits<uint32_t>::max() / sizeof(uint16_t));

  const uint32_t transfer_size = sample_count * sizeof(uint16_t);
  REQUIRE(buffer_size >= transfer_size);
  REQUIRE(IS_DMA_BLOCK_SIZE(transfer_size) != 0U);

  node_config.NodeType = DMA_GPDMA_LINEAR_NODE;
  node_config.SrcAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&adc_handle->Instance->DR));
  node_config.DstAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
  node_config.DataSize = transfer_size;

  REQUIRE(state_.queue.Head == nullptr);
  REQUIRE(state_.queue.NodeNumber == 0U);
  REQUIRE(HAL_DMAEx_List_BuildNode(&node_config, &state_.node) == HAL_OK);
  REQUIRE(HAL_DMAEx_List_InsertNode_Tail(&state_.queue, &state_.node) == HAL_OK);
  REQUIRE(HAL_DMAEx_List_SetCircularMode(&state_.queue) == HAL_OK);
  REQUIRE(state_.queue.Head == &state_.node);
  REQUIRE(state_.queue.FirstCircularNode == &state_.node);
  REQUIRE(state_.queue.NodeNumber == 1U);
  REQUIRE(state_.queue.State == HAL_DMA_QUEUE_STATE_READY);

  REQUIRE(HAL_DMAEx_List_UnLinkQ(dma_handle) == HAL_OK);
  REQUIRE(HAL_DMAEx_List_LinkQ(dma_handle, &state_.queue) == HAL_OK);
  REQUIRE(dma_handle->LinkedListQueue == &state_.queue);
}

}  // namespace LibXR

#endif
