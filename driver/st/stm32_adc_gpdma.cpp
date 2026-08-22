#include "stm32_adc_gpdma.hpp"

#if defined(LIBXR_STM32_ADC_GPDMA) && defined(HAL_ADC_MODULE_ENABLED)

#include <limits>

#include "libxr_def.hpp"
#include "stm32_dcache.hpp"

namespace LibXR
{

namespace
{

bool IsValidAdcNode(const DMA_NodeConfTypeDef& config)
{
  return config.Init.Direction == DMA_PERIPH_TO_MEMORY &&
         config.Init.SrcInc == DMA_SINC_FIXED &&
         config.Init.DestInc == DMA_DINC_INCREMENTED &&
         config.Init.SrcDataWidth == DMA_SRC_DATAWIDTH_HALFWORD &&
         config.Init.DestDataWidth == DMA_DEST_DATAWIDTH_HALFWORD &&
         config.Init.SrcBurstLength == 1U && config.Init.DestBurstLength == 1U &&
         config.Init.BlkHWRequest == DMA_BREQ_SINGLE_BURST &&
         config.Init.Mode == DMA_NORMAL &&
         config.TriggerConfig.TriggerPolarity == DMA_TRIG_POLARITY_MASKED &&
         config.DataHandlingConfig.DataExchange == DMA_EXCHANGE_NONE;
}

}  // namespace

HAL_StatusTypeDef STM32GpdmaAdcAdapter::Start(ADC_HandleTypeDef* adc_handle,
                                              uint32_t* buffer, uint32_t sample_count,
                                              size_t buffer_size)
{
  DMA_HandleTypeDef* const dma_handle = adc_handle->DMA_Handle;
  ASSERT(dma_handle->Parent == adc_handle &&
         IS_GPDMA_INSTANCE(dma_handle->Instance) != 0U);
  ASSERT(dma_handle->InitLinkedList.LinkStepMode == DMA_LSM_FULL_EXECUTION);
  ASSERT(dma_handle->LinkedListQueue != nullptr &&
         dma_handle->LinkedListQueue != &state_.queue &&
         dma_handle->LinkedListQueue->Head != nullptr);

  const HAL_StatusTypeDef status =
      BuildAndAttach(adc_handle, buffer, sample_count, buffer_size);
  if (status != HAL_OK)
  {
    return status;
  }

  STM32_CleanDCacheByAddr(&state_, sizeof(state_));
  STM32_CleanDCacheByAddr(buffer, buffer_size);
  STM32_InvalidateDCacheByAddr(buffer, buffer_size);

  return HAL_ADC_Start_DMA(adc_handle, buffer, sample_count);
}

HAL_StatusTypeDef STM32GpdmaAdcAdapter::BuildAndAttach(ADC_HandleTypeDef* adc_handle,
                                                       uint32_t* buffer,
                                                       uint32_t sample_count,
                                                       size_t buffer_size)
{
  DMA_HandleTypeDef* const dma_handle = adc_handle->DMA_Handle;

  DMA_NodeConfTypeDef node_config{};
  HAL_StatusTypeDef status =
      HAL_DMAEx_List_GetNodeConfig(&node_config, dma_handle->LinkedListQueue->Head);
  if (status != HAL_OK)
  {
    return status;
  }
  ASSERT(IsValidAdcNode(node_config));

  const uint64_t transfer_size = static_cast<uint64_t>(sample_count) * sizeof(uint16_t);
  ASSERT(buffer != nullptr && sample_count > 0U &&
         transfer_size <= std::numeric_limits<uint32_t>::max() &&
         buffer_size >= transfer_size &&
         IS_DMA_BLOCK_SIZE(static_cast<uint32_t>(transfer_size)) != 0U);

  node_config.NodeType = DMA_GPDMA_LINEAR_NODE;
  node_config.SrcAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&adc_handle->Instance->DR));
  node_config.DstAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
  node_config.DataSize = static_cast<uint32_t>(transfer_size);

  const bool attached =
      HAL_DMAEx_List_BuildNode(&node_config, &state_.node) == HAL_OK &&
      HAL_DMAEx_List_InsertNode_Tail(&state_.queue, &state_.node) == HAL_OK &&
      HAL_DMAEx_List_SetCircularMode(&state_.queue) == HAL_OK &&
      HAL_DMAEx_List_UnLinkQ(dma_handle) == HAL_OK &&
      HAL_DMAEx_List_LinkQ(dma_handle, &state_.queue) == HAL_OK;
  return attached ? HAL_OK : HAL_ERROR;
}

}  // namespace LibXR

#endif
