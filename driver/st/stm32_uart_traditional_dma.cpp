#include "stm32_uart.hpp"

#if defined(HAL_UART_MODULE_ENABLED) && !defined(LIBXR_STM32_UART_GPDMA)

using namespace LibXR;

namespace
{
class TraditionalDmaAdapter
{
 public:
  using AbortCallback = void (*)(DMA_HandleTypeDef* dma_handle);

  [[nodiscard]] static bool IsStopped(DMA_HandleTypeDef* dma_handle)
  {
#if defined(DMA_SxCR_EN) && defined(BDMA_CCR_EN) && defined(IS_DMA_STREAM_INSTANCE)
    if (IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U)
    {
      return (static_cast<DMA_Stream_TypeDef*>(dma_handle->Instance)->CR & DMA_SxCR_EN) ==
             0U;
    }
    return (static_cast<BDMA_Channel_TypeDef*>(dma_handle->Instance)->CCR &
            BDMA_CCR_EN) == 0U;
#elif defined(DMA_SxCR_EN)
    return (dma_handle->Instance->CR & DMA_SxCR_EN) == 0U;
#elif defined(DMA_CCR_EN)
    return (dma_handle->Instance->CCR & DMA_CCR_EN) == 0U;
#else
    (void)dma_handle;
    return false;
#endif
  }

  [[nodiscard]] static bool StopComplete(DMA_HandleTypeDef* dma_handle)
  {
    return IsStopped(dma_handle) && (dma_handle->State == HAL_DMA_STATE_READY);
  }

  [[nodiscard]] static bool LaunchStop(DMA_HandleTypeDef* dma_handle,
                                       AbortCallback callback, bool* tx_evidence_captured,
                                       bool* tx_payload_complete, bool* tx_error)
  {
    ASSERT((tx_evidence_captured == nullptr) == (tx_payload_complete == nullptr));
    ASSERT((tx_evidence_captured == nullptr) == (tx_error == nullptr));
    const auto launch = [&]()
    {
      if (StopComplete(dma_handle))
      {
        if (tx_evidence_captured != nullptr)
        {
          CaptureStoppedTx(dma_handle, *tx_evidence_captured, *tx_payload_complete,
                           *tx_error);
        }
        return true;
      }

#if defined(DMA_SxCR_EN)
      if (IsAsyncStream(dma_handle))
      {
#ifdef DMA_IT_TC
        const auto* stream = static_cast<DMA_Stream_TypeDef*>(dma_handle->Instance);
        if ((stream->CR & DMA_IT_TC) == 0U)
        {
          return false;
        }
#endif
      }

      if (IsAsyncStream(dma_handle) && (dma_handle->State == HAL_DMA_STATE_ABORT))
      {
        dma_handle->XferAbortCallback = callback;
        return true;
      }
#endif

      // Channel DMA and H7 BDMA clear their flags inside HAL_DMA_Abort_IT(). Freeze
      // and classify them first so a last-beat terminal cannot be destroyed by HAL.
#if defined(DMA_SxCR_EN)
      if (!IsAsyncStream(dma_handle))
#endif
      {
        DisableInterrupts(dma_handle);
        __HAL_DMA_DISABLE(dma_handle);
        ReadBackControl(dma_handle);
        __DSB();
        if (tx_evidence_captured != nullptr)
        {
          CaptureStoppedTx(dma_handle, *tx_evidence_captured, *tx_payload_complete,
                           *tx_error);
        }
      }

      dma_handle->XferAbortCallback = callback;
      const HAL_StatusTypeDef result = HAL_DMA_Abort_IT(dma_handle);
      bool accepted = result == HAL_OK;
#if defined(DMA_SxCR_EN)
      accepted = accepted || (IsAsyncStream(dma_handle) &&
                              (dma_handle->State == HAL_DMA_STATE_ABORT));
#endif
      return accepted || StopComplete(dma_handle);
    };

#if defined(DMA_SxCR_EN)
    if (IsAsyncStream(dma_handle))
    {
      // F4/H7 HAL checks BUSY before publishing ABORT. Preserve a pending terminal
      // flag until the Stream NVIC vector is restored after that transition.
      StreamNvicMaskGuard irq_guard(dma_handle);
      if (!irq_guard.Valid())
      {
        return false;
      }
      return launch();
    }
#endif
    return launch();
  }

  static void CaptureStoppedTx(DMA_HandleTypeDef* dma_handle, bool& evidence_captured,
                               bool& payload_complete, bool& error)
  {
    ASSERT(IsStopped(dma_handle));
    const bool observed_payload_complete = __HAL_DMA_GET_COUNTER(dma_handle) == 0U;
    if (evidence_captured)
    {
      ASSERT(payload_complete == observed_payload_complete);
    }
    else
    {
      payload_complete = observed_payload_complete;
      evidence_captured = true;
    }
    error = error || HasTransferError(dma_handle);
  }

  static void FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr)
  {
    REQUIRE_FROM_CALLBACK(StopComplete(dma_handle), in_isr);
    REQUIRE_FROM_CALLBACK(dma_handle->Lock == HAL_UNLOCKED, in_isr);
    if (!StopComplete(dma_handle)) return;

    DisableInterrupts(dma_handle);
    ReadBackControl(dma_handle);
    __DSB();
    ClearFlags(dma_handle);
    __DSB();
  }

 private:
#if defined(DMA_SxCR_EN)
  [[nodiscard]] static bool IsAsyncStream(DMA_HandleTypeDef* dma_handle)
  {
#if defined(IS_DMA_STREAM_INSTANCE)
    return IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U;
#else
    (void)dma_handle;
    return true;
#endif
  }

  [[nodiscard]] static IRQn_Type StreamIrq(DMA_HandleTypeDef* dma_handle)
  {
#ifdef DMA1_Stream0
    if (dma_handle->Instance == DMA1_Stream0) return DMA1_Stream0_IRQn;
#endif
#ifdef DMA1_Stream1
    if (dma_handle->Instance == DMA1_Stream1) return DMA1_Stream1_IRQn;
#endif
#ifdef DMA1_Stream2
    if (dma_handle->Instance == DMA1_Stream2) return DMA1_Stream2_IRQn;
#endif
#ifdef DMA1_Stream3
    if (dma_handle->Instance == DMA1_Stream3) return DMA1_Stream3_IRQn;
#endif
#ifdef DMA1_Stream4
    if (dma_handle->Instance == DMA1_Stream4) return DMA1_Stream4_IRQn;
#endif
#ifdef DMA1_Stream5
    if (dma_handle->Instance == DMA1_Stream5) return DMA1_Stream5_IRQn;
#endif
#ifdef DMA1_Stream6
    if (dma_handle->Instance == DMA1_Stream6) return DMA1_Stream6_IRQn;
#endif
#ifdef DMA1_Stream7
    if (dma_handle->Instance == DMA1_Stream7) return DMA1_Stream7_IRQn;
#endif
#ifdef DMA2_Stream0
    if (dma_handle->Instance == DMA2_Stream0) return DMA2_Stream0_IRQn;
#endif
#ifdef DMA2_Stream1
    if (dma_handle->Instance == DMA2_Stream1) return DMA2_Stream1_IRQn;
#endif
#ifdef DMA2_Stream2
    if (dma_handle->Instance == DMA2_Stream2) return DMA2_Stream2_IRQn;
#endif
#ifdef DMA2_Stream3
    if (dma_handle->Instance == DMA2_Stream3) return DMA2_Stream3_IRQn;
#endif
#ifdef DMA2_Stream4
    if (dma_handle->Instance == DMA2_Stream4) return DMA2_Stream4_IRQn;
#endif
#ifdef DMA2_Stream5
    if (dma_handle->Instance == DMA2_Stream5) return DMA2_Stream5_IRQn;
#endif
#ifdef DMA2_Stream6
    if (dma_handle->Instance == DMA2_Stream6) return DMA2_Stream6_IRQn;
#endif
#ifdef DMA2_Stream7
    if (dma_handle->Instance == DMA2_Stream7) return DMA2_Stream7_IRQn;
#endif
    ASSERT(false);
    return NonMaskableInt_IRQn;
  }

  class StreamNvicMaskGuard
  {
   public:
    explicit StreamNvicMaskGuard(DMA_HandleTypeDef* dma_handle)
        : irq_(StreamIrq(dma_handle)), valid_(static_cast<int32_t>(irq_) >= 0)
    {
      ASSERT(valid_);
      if (!valid_)
      {
        return;
      }

      if (NVIC_GetEnableIRQ(irq_) == 0U)
      {
        // HAL_DMA_Abort_IT() completes Stream aborts only through this vector.
        // An originally disabled vector therefore has no legal STOP_DONE carrier.
        valid_ = false;
        return;
      }

      restore_ = true;
      NVIC_DisableIRQ(irq_);
      __DSB();
      __ISB();
    }

    ~StreamNvicMaskGuard()
    {
      if (!valid_)
      {
        return;
      }

      __DSB();
      if (restore_)
      {
        NVIC_EnableIRQ(irq_);
      }
      __ISB();
    }

    StreamNvicMaskGuard(const StreamNvicMaskGuard&) = delete;
    StreamNvicMaskGuard& operator=(const StreamNvicMaskGuard&) = delete;

    [[nodiscard]] bool Valid() const { return valid_; }

   private:
    IRQn_Type irq_;
    bool valid_;
    bool restore_ = false;
  };
#endif

  [[maybe_unused]] static bool HasDmamux(DMA_HandleTypeDef* dma_handle)
  {
#if defined(DMAMUX_CxCR_SOIE)
#if defined(IS_DMA_DMAMUX_ALL_INSTANCE)
    return IS_DMA_DMAMUX_ALL_INSTANCE(dma_handle->Instance) != 0U;
#else
    (void)dma_handle;
    return true;
#endif
#else
    (void)dma_handle;
    return false;
#endif
  }

  static void DisableDmamuxInterrupts(DMA_HandleTypeDef* dma_handle)
  {
#if defined(DMAMUX_CxCR_SOIE)
    if (HasDmamux(dma_handle) && (dma_handle->DMAmuxChannel != nullptr))
    {
      dma_handle->DMAmuxChannel->CCR &= ~DMAMUX_CxCR_SOIE;
#if defined(DMAMUX_RGxCR_OIE)
      if (dma_handle->DMAmuxRequestGen != nullptr)
      {
        dma_handle->DMAmuxRequestGen->RGCR &= ~DMAMUX_RGxCR_OIE;
      }
#endif
    }
#else
    (void)dma_handle;
#endif
  }

  static void ClearDmamuxFlags(DMA_HandleTypeDef* dma_handle)
  {
#if defined(DMAMUX_CxCR_SOIE)
    if (HasDmamux(dma_handle) && (dma_handle->DMAmuxChannelStatus != nullptr))
    {
      dma_handle->DMAmuxChannelStatus->CFR = dma_handle->DMAmuxChannelStatusMask;
#if defined(DMAMUX_RGxCR_OIE)
      if ((dma_handle->DMAmuxRequestGen != nullptr) &&
          (dma_handle->DMAmuxRequestGenStatus != nullptr))
      {
        dma_handle->DMAmuxRequestGenStatus->RGCFR =
            dma_handle->DMAmuxRequestGenStatusMask;
      }
#endif
    }
#else
    (void)dma_handle;
#endif
  }

  static void DisableInterrupts(DMA_HandleTypeDef* dma_handle)
  {
#if defined(__HAL_DMA_DISABLE_IT)
    uint32_t mask = 0U;
#ifdef DMA_IT_TC
    mask |= DMA_IT_TC;
#endif
#ifdef DMA_IT_HT
    mask |= DMA_IT_HT;
#endif
#ifdef DMA_IT_TE
    mask |= DMA_IT_TE;
#endif
#ifdef DMA_IT_DME
    mask |= DMA_IT_DME;
#endif
    if (mask != 0U) __HAL_DMA_DISABLE_IT(dma_handle, mask);
#ifdef DMA_IT_FE
    __HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_FE);
#endif
#else
    (void)dma_handle;
#endif
    DisableDmamuxInterrupts(dma_handle);
  }

  static void ClearFlags(DMA_HandleTypeDef* dma_handle)
  {
#if defined(__HAL_DMA_CLEAR_FLAG)
    uint32_t flags = 0U;
#ifdef __HAL_DMA_GET_TC_FLAG_INDEX
    flags |= __HAL_DMA_GET_TC_FLAG_INDEX(dma_handle);
#endif
#ifdef __HAL_DMA_GET_HT_FLAG_INDEX
    flags |= __HAL_DMA_GET_HT_FLAG_INDEX(dma_handle);
#endif
#ifdef __HAL_DMA_GET_TE_FLAG_INDEX
    flags |= __HAL_DMA_GET_TE_FLAG_INDEX(dma_handle);
#endif
#ifdef __HAL_DMA_GET_DME_FLAG_INDEX
    flags |= __HAL_DMA_GET_DME_FLAG_INDEX(dma_handle);
#endif
#ifdef __HAL_DMA_GET_FE_FLAG_INDEX
    flags |= __HAL_DMA_GET_FE_FLAG_INDEX(dma_handle);
#endif
    if (flags != 0U) __HAL_DMA_CLEAR_FLAG(dma_handle, flags);
    ClearDmamuxFlags(dma_handle);
#else
    (void)dma_handle;
#endif
  }

  [[nodiscard]] static bool HasTransferError(DMA_HandleTypeDef* dma_handle)
  {
    uint32_t error_code = dma_handle->ErrorCode;
#ifdef HAL_DMA_ERROR_NO_XFER
    error_code &= ~HAL_DMA_ERROR_NO_XFER;
#endif
    bool error = error_code != HAL_DMA_ERROR_NONE;

#if defined(__HAL_DMA_GET_FLAG)
#ifdef __HAL_DMA_GET_TE_FLAG_INDEX
    error = error || (__HAL_DMA_GET_FLAG(
                          dma_handle, __HAL_DMA_GET_TE_FLAG_INDEX(dma_handle)) != RESET);
#endif
#ifdef __HAL_DMA_GET_DME_FLAG_INDEX
    error = error || (__HAL_DMA_GET_FLAG(
                          dma_handle, __HAL_DMA_GET_DME_FLAG_INDEX(dma_handle)) != RESET);
#endif
#ifdef __HAL_DMA_GET_FE_FLAG_INDEX
    error = error || (__HAL_DMA_GET_FLAG(
                          dma_handle, __HAL_DMA_GET_FE_FLAG_INDEX(dma_handle)) != RESET);
#endif
#endif

#if defined(DMAMUX_CxCR_SOIE)
    if (HasDmamux(dma_handle) && (dma_handle->DMAmuxChannelStatus != nullptr))
    {
      error = error || ((dma_handle->DMAmuxChannelStatus->CSR &
                         dma_handle->DMAmuxChannelStatusMask) != 0U);
    }
#if defined(DMAMUX_RGxCR_OIE)
    if ((dma_handle->DMAmuxRequestGen != nullptr) &&
        (dma_handle->DMAmuxRequestGenStatus != nullptr))
    {
      error = error || ((dma_handle->DMAmuxRequestGenStatus->RGSR &
                         dma_handle->DMAmuxRequestGenStatusMask) != 0U);
    }
#endif
#endif
    return error;
  }

  static void ReadBackControl(DMA_HandleTypeDef* dma_handle)
  {
#if defined(DMA_SxCR_EN) && defined(BDMA_CCR_EN) && defined(IS_DMA_STREAM_INSTANCE)
    if (IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U)
    {
      const volatile uint32_t value =
          static_cast<DMA_Stream_TypeDef*>(dma_handle->Instance)->CR;
      UNUSED(value);
    }
    else
    {
      const volatile uint32_t value =
          static_cast<BDMA_Channel_TypeDef*>(dma_handle->Instance)->CCR;
      UNUSED(value);
    }
#elif defined(DMA_SxCR_EN)
    const volatile uint32_t value = dma_handle->Instance->CR;
    UNUSED(value);
#elif defined(DMA_CCR_EN)
    const volatile uint32_t value = dma_handle->Instance->CCR;
    UNUSED(value);
#else
    (void)dma_handle;
#endif
  }
};
}  // namespace

bool STM32UART::AllDmaStopsComplete() const
{
  return ((uart_handle_->hdmatx == nullptr) ||
          TraditionalDmaAdapter::StopComplete(uart_handle_->hdmatx)) &&
         ((uart_handle_->hdmarx == nullptr) ||
          TraditionalDmaAdapter::StopComplete(uart_handle_->hdmarx));
}

void STM32UART::CloseTxTerminalSource()
{
  ATOMIC_CLEAR_BIT(uart_handle_->Instance->CR1, USART_CR1_TCIE);
  ATOMIC_CLEAR_BIT(uart_handle_->Instance->CR3, USART_CR3_DMAT);
  const volatile uint32_t cr1 = uart_handle_->Instance->CR1;
  const volatile uint32_t cr3 = uart_handle_->Instance->CR3;
  UNUSED(cr1);
  UNUSED(cr3);
  __DSB();
}

void STM32UART::LaunchDmaStop(DMA_HandleTypeDef* dma_handle, bool in_isr,
                              bool classify_tx)
{
  ASSERT(dma_handle != nullptr);
  ASSERT(dma_handle->Parent == uart_handle_);
  const bool accepted = TraditionalDmaAdapter::LaunchStop(
      dma_handle, DmaAbortCallback, classify_tx ? &tx_evidence_captured_ : nullptr,
      classify_tx ? &tx_payload_complete_ : nullptr,
      classify_tx ? &tx_dma_error_ : nullptr);
  REQUIRE_FROM_CALLBACK(accepted, in_isr);
}

void STM32UART::CaptureStoppedTx()
{
  ASSERT(uart_handle_->hdmatx != nullptr);
  TraditionalDmaAdapter::CaptureStoppedTx(uart_handle_->hdmatx, tx_evidence_captured_,
                                          tx_payload_complete_, tx_dma_error_);
}

void STM32UART::FinalizeStopped(DMA_HandleTypeDef* dma_handle, bool in_isr)
{
  TraditionalDmaAdapter::FinalizeStopped(dma_handle, in_isr);
}

#endif
