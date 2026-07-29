#include "stm32_uart_h5_gpdma.hpp"

#if defined(STM32H5) && defined(HAL_UART_MODULE_ENABLED)

#include "libxr_assert.hpp"
#include "stm32_dcache.hpp"

namespace LibXR
{
namespace
{

IRQn_Type GetGpdmaIrq(DMA_Channel_TypeDef* instance)
{
#define LIBXR_GPDMA_IRQ_CASE(DMA, CHANNEL) \
  if (instance == DMA##_Channel##CHANNEL)  \
  {                                        \
    return DMA##_Channel##CHANNEL##_IRQn;  \
  }

  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 0)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 1)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 2)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 3)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 4)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 5)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 6)
  LIBXR_GPDMA_IRQ_CASE(GPDMA1, 7)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 0)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 1)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 2)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 3)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 4)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 5)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 6)
  LIBXR_GPDMA_IRQ_CASE(GPDMA2, 7)

#undef LIBXR_GPDMA_IRQ_CASE

  ASSERT(false);
  return NonMaskableInt_IRQn;
}

class GpdmaNvicMaskGuard
{
 public:
  explicit GpdmaNvicMaskGuard(DMA_HandleTypeDef* dma_handle)
      : irq_(GetGpdmaIrq(dma_handle->Instance)),
        valid_(static_cast<int32_t>(irq_) >= 0),
        was_enabled_(valid_ && (NVIC_GetEnableIRQ(irq_) != 0U))
  {
    if (!valid_)
    {
      return;
    }

    if (was_enabled_)
    {
      NVIC_DisableIRQ(irq_);
      __DSB();
      __ISB();
    }
  }

  ~GpdmaNvicMaskGuard()
  {
    if (valid_ && was_enabled_)
    {
      NVIC_EnableIRQ(irq_);
      __DSB();
      __ISB();
    }
  }

  GpdmaNvicMaskGuard(const GpdmaNvicMaskGuard&) = delete;
  GpdmaNvicMaskGuard& operator=(const GpdmaNvicMaskGuard&) = delete;

  [[nodiscard]] bool Valid() const { return valid_; }
  [[nodiscard]] bool WasEnabled() const { return was_enabled_; }

 private:
  IRQn_Type irq_;
  bool valid_;
  bool was_enabled_;
};

}  // namespace

STM32H5GpdmaUartAdapter::STM32H5GpdmaUartAdapter(UART_HandleTypeDef* uart_handle)
{
  REQUIRE(uart_handle != nullptr);
  state_.uart_handle = uart_handle;
}

HAL_StatusTypeDef STM32H5GpdmaUartAdapter::StartLinkedListDmaRx(uint8_t* buffer,
                                                                size_t total_size,
                                                                size_t descriptor_count,
                                                                bool in_isr)
{
  REQUIRE_FROM_CALLBACK(state_.uart_handle->hdmarx != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(buffer != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(descriptor_count == RX_NODE_COUNT, in_isr);
  REQUIRE_FROM_CALLBACK(total_size >= RX_NODE_COUNT, in_isr);
  REQUIRE_FROM_CALLBACK((total_size % RX_NODE_COUNT) == 0U, in_isr);
  REQUIRE_FROM_CALLBACK((total_size / RX_NODE_COUNT) <= UINT16_MAX, in_isr);
  REQUIRE_FROM_CALLBACK(IS_DMA_BLOCK_SIZE(total_size / RX_NODE_COUNT) != 0U, in_isr);
  REQUIRE_FROM_CALLBACK(state_.uart_handle->hdmarx->Mode == DMA_LINKEDLIST_CIRCULAR,
                        in_isr);

  if (!state_.rx_queue_built)
  {
    BuildRxQueue(buffer, total_size, descriptor_count, in_isr);
  }
  else
  {
    REQUIRE_FROM_CALLBACK(buffer == state_.rx_buffer, in_isr);
    REQUIRE_FROM_CALLBACK(total_size == state_.rx_total_size, in_isr);
    REQUIRE_FROM_CALLBACK(state_.uart_handle->hdmarx->LinkedListQueue == &state_.rx_queue,
                          in_isr);
  }

  REQUIRE_FROM_CALLBACK(StopComplete(state_.uart_handle->hdmarx), in_isr);
  FinalizeStopped(state_.uart_handle->hdmarx, in_isr);
  STM32_CleanDCacheByAddr(buffer, total_size);
  STM32_InvalidateDCacheByAddr(buffer, total_size);

  return HAL_UARTEx_ReceiveToIdle_DMA(state_.uart_handle, buffer,
                                      static_cast<uint16_t>(state_.rx_node_size));
}

uint8_t* STM32H5GpdmaUartAdapter::GetLinkedListDmaRxProducer() const
{
  ASSERT(state_.uart_handle->hdmarx != nullptr);
  const uintptr_t destination = state_.uart_handle->hdmarx->Instance->CDAR;
  return reinterpret_cast<uint8_t*>(destination);
}

void STM32H5GpdmaUartAdapter::PrepareLinkedListDmaRxForCpu(uint8_t* data, size_t size)
{
  STM32_InvalidateDCacheByAddr(data, size);
}

void STM32H5GpdmaUartAdapter::CloseTxTerminalSource() const
{
  ATOMIC_CLEAR_BIT(state_.uart_handle->Instance->CR1, USART_CR1_TCIE);
  const volatile uint32_t cr1 = state_.uart_handle->Instance->CR1;
  UNUSED(cr1);
  __DSB();
}

bool STM32H5GpdmaUartAdapter::LaunchStop(DMA_HandleTypeDef* dma_handle,
                                         AbortCallback callback, bool in_isr)
{
  ASSERT(dma_handle != nullptr);
  ASSERT(dma_handle->Parent == state_.uart_handle);
  ASSERT(callback != nullptr);

  const auto abort_is_joinable = [&]()
  {
    if (dma_handle->XferAbortCallback == callback)
    {
      return true;
    }

    // A UART line error may have started the RX abort before LibXR reaches this
    // boundary. Preserve HAL's callback: it publishes ERROR through
    // HAL_UART_ErrorCallback(), which is a carrier for the same service.
    return (dma_handle == state_.uart_handle->hdmarx) &&
           (state_.uart_handle->ErrorCode != HAL_UART_ERROR_NONE) &&
           (dma_handle->XferAbortCallback != nullptr);
  };

  if (StopComplete(dma_handle))
  {
    return true;
  }

  GpdmaNvicMaskGuard irq_guard(dma_handle);
  REQUIRE_FROM_CALLBACK(irq_guard.Valid() && irq_guard.WasEnabled(), in_isr);
  if (!irq_guard.Valid() || !irq_guard.WasEnabled())
  {
    return false;
  }

  // The related HAL IRQ may have finished between the first state check and the NVIC
  // mask. Re-evaluate only after its handler can no longer enter.
  if (StopComplete(dma_handle))
  {
    return true;
  }
  if (dma_handle->State == HAL_DMA_STATE_ABORT)
  {
    const bool joined = abort_is_joinable();
    REQUIRE_FROM_CALLBACK(joined, in_isr);
    return joined;
  }
  if (dma_handle->State != HAL_DMA_STATE_BUSY)
  {
    return false;
  }

  AbortCallback const previous_callback = dma_handle->XferAbortCallback;
  const HAL_StatusTypeDef result = HAL_DMA_Abort_IT(dma_handle);
  if (result == HAL_OK)
  {
    // The DMA vector is still masked, so LibXR may install its callback after it
    // publishes ABORT. A concurrent UART line-error handler owns the abort instead
    // when it changed the callback while HAL_DMA_Abort_IT() was running.
    if (dma_handle->XferAbortCallback == previous_callback)
    {
      dma_handle->XferAbortCallback = callback;
      return true;
    }

    const bool joined = abort_is_joinable();
    REQUIRE_FROM_CALLBACK(joined, in_isr);
    return joined;
  }

  return ((dma_handle->State == HAL_DMA_STATE_ABORT) && abort_is_joinable()) ||
         StopComplete(dma_handle);
}

bool STM32H5GpdmaUartAdapter::StopComplete(DMA_HandleTypeDef* dma_handle)
{
  if ((dma_handle == nullptr) || !IsStopped(dma_handle) ||
      (dma_handle->State != HAL_DMA_STATE_READY))
  {
    return false;
  }
  if ((dma_handle->Mode & DMA_LINKEDLIST) == DMA_LINKEDLIST)
  {
    return (dma_handle->LinkedListQueue != nullptr) &&
           (dma_handle->LinkedListQueue->State == HAL_DMA_QUEUE_STATE_READY);
  }
  return true;
}

bool STM32H5GpdmaUartAdapter::AllStopsComplete() const
{
  return ((state_.uart_handle->hdmatx == nullptr) ||
          StopComplete(state_.uart_handle->hdmatx)) &&
         ((state_.uart_handle->hdmarx == nullptr) ||
          StopComplete(state_.uart_handle->hdmarx));
}

void STM32H5GpdmaUartAdapter::CaptureStoppedTx(DMA_HandleTypeDef* dma_handle,
                                               bool& evidence_captured,
                                               bool& payload_complete, bool& error) const
{
  ASSERT(dma_handle == state_.uart_handle->hdmatx);
  ASSERT(StopComplete(dma_handle));

  // CBR1 is intentionally sampled before HAL_UART_Abort() overwrites ErrorCode with
  // NO_XFER. Until H503 reset readback is proven on hardware, even zero is not used as
  // authoritative payload completion.
  const uint32_t remaining = __HAL_DMA_GET_COUNTER(dma_handle);
  UNUSED(remaining);

  if (!evidence_captured)
  {
    payload_complete = false;
    evidence_captured = true;
  }
  else
  {
    ASSERT(!payload_complete);
  }
  error = error || HasTransferError(dma_handle);
}

void STM32H5GpdmaUartAdapter::FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(StopComplete(dma_handle), in_isr);
  REQUIRE_FROM_CALLBACK(dma_handle->Lock == HAL_UNLOCKED, in_isr);
  if (!StopComplete(dma_handle))
  {
    return;
  }

  DisableInterrupts(dma_handle);
  if ((dma_handle->Mode & DMA_LINKEDLIST) == DMA_LINKEDLIST)
  {
    // The H5 GPDMA error IRQ path resets the channel without clearing CBR1.
    // A zero block count forces the next list start to load the head node.
    dma_handle->Instance->CBR1 = 0U;
  }
  const volatile uint32_t ccr = dma_handle->Instance->CCR;
  UNUSED(ccr);
  __DSB();
  ClearFlags(dma_handle);
  __DSB();
}

bool STM32H5GpdmaUartAdapter::IsStopped(DMA_HandleTypeDef* dma_handle)
{
  const uint32_t ccr = dma_handle->Instance->CCR;
  return (ccr & DMA_CCR_EN) == 0U;
}

bool STM32H5GpdmaUartAdapter::HasTransferError(DMA_HandleTypeDef* dma_handle)
{
  uint32_t error_code = dma_handle->ErrorCode;
  error_code &= ~HAL_DMA_ERROR_NO_XFER;
  constexpr uint32_t error_flags =
      DMA_FLAG_DTE | DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_TO;
  return (error_code != HAL_DMA_ERROR_NONE) ||
         ((__HAL_DMA_GET_FLAG(dma_handle, error_flags)) != 0U);
}

void STM32H5GpdmaUartAdapter::DisableInterrupts(DMA_HandleTypeDef* dma_handle)
{
  __HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_TC | DMA_IT_HT | DMA_IT_DTE | DMA_IT_ULE |
                                       DMA_IT_USE | DMA_IT_SUSP | DMA_IT_TO);
}

void STM32H5GpdmaUartAdapter::ClearFlags(DMA_HandleTypeDef* dma_handle)
{
  __HAL_DMA_CLEAR_FLAG(dma_handle, DMA_FLAG_TC | DMA_FLAG_HT | DMA_FLAG_DTE |
                                       DMA_FLAG_ULE | DMA_FLAG_USE | DMA_FLAG_SUSP |
                                       DMA_FLAG_TO);
}

void STM32H5GpdmaUartAdapter::BuildRxQueue(uint8_t* buffer, size_t total_size,
                                           size_t descriptor_count, bool in_isr)
{
  DMA_HandleTypeDef* const dma_handle = state_.uart_handle->hdmarx;
  REQUIRE_FROM_CALLBACK(StopComplete(dma_handle), in_isr);
  REQUIRE_FROM_CALLBACK(dma_handle->LinkedListQueue != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(dma_handle->LinkedListQueue->Head != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(descriptor_count == RX_NODE_COUNT, in_isr);

  DMA_NodeConfTypeDef node_config{};
  REQUIRE_FROM_CALLBACK(HAL_DMAEx_List_GetNodeConfig(
                            &node_config, dma_handle->LinkedListQueue->Head) == HAL_OK,
                        in_isr);

  // UART RX nodes are contiguous one-dimensional blocks. Do not inherit a
  // CubeMX seed node's unused 2D repeat-address fields.
  node_config.NodeType = DMA_GPDMA_LINEAR_NODE;

  const size_t node_size = total_size / descriptor_count;
  node_config.SrcAddress = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&state_.uart_handle->Instance->RDR));
  node_config.DataSize = static_cast<uint32_t>(node_size);

  for (size_t i = 0U; i < RX_NODE_COUNT; ++i)
  {
    node_config.DstAddress =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&buffer[i * node_size]));
    REQUIRE_FROM_CALLBACK(
        HAL_DMAEx_List_BuildNode(&node_config, &state_.nodes[i]) == HAL_OK, in_isr);
    REQUIRE_FROM_CALLBACK(
        HAL_DMAEx_List_InsertNode_Tail(&state_.rx_queue, &state_.nodes[i]) == HAL_OK,
        in_isr);
  }

  REQUIRE_FROM_CALLBACK(HAL_DMAEx_List_SetCircularMode(&state_.rx_queue) == HAL_OK,
                        in_isr);
  REQUIRE_FROM_CALLBACK(HAL_DMAEx_List_UnLinkQ(dma_handle) == HAL_OK, in_isr);
  REQUIRE_FROM_CALLBACK(HAL_DMAEx_List_LinkQ(dma_handle, &state_.rx_queue) == HAL_OK,
                        in_isr);
  REQUIRE_FROM_CALLBACK(dma_handle->LinkedListQueue == &state_.rx_queue, in_isr);

  state_.rx_buffer = buffer;
  state_.rx_total_size = total_size;
  state_.rx_node_size = node_size;
  state_.rx_queue_built = true;
}

}  // namespace LibXR

#endif
