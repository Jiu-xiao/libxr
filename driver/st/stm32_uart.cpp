#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

STM32UART* STM32UART::map[STM32_UART_NUMBER] = {nullptr};

namespace
{
constexpr bool ShouldPublishStopDoneOnTxComplete(bool stop_active,
                                                 bool waiting_for_uart_tc)
{
  return stop_active && waiting_for_uart_tc;
}

static_assert(ShouldPublishStopDoneOnTxComplete(true, true));
static_assert(!ShouldPublishStopDoneOnTxComplete(true, false));
static_assert(!ShouldPublishStopDoneOnTxComplete(false, true));

constexpr UartOldTxTerminal PreserveRequiredReplay(bool replay_required,
                                                   UartOldTxTerminal terminal)
{
  return replay_required && (terminal == UartOldTxTerminal::COMPLETE)
             ? UartOldTxTerminal::NONE
             : terminal;
}

static_assert(PreserveRequiredReplay(true, UartOldTxTerminal::COMPLETE) ==
              UartOldTxTerminal::NONE);
static_assert(PreserveRequiredReplay(true, UartOldTxTerminal::ERROR) ==
              UartOldTxTerminal::ERROR);
static_assert(PreserveRequiredReplay(false, UartOldTxTerminal::COMPLETE) ==
              UartOldTxTerminal::COMPLETE);

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

[[nodiscard]] uint32_t GetUartKernelClock(UART_HandleTypeDef* uart_handle)
{
#if defined(UART_GETCLOCKSOURCE)
#if defined(STM32U0xx_HAL_H)
  // U0 returns an RCC_PERIPHCLK_* selector rather than a UART clock-source enum.
  uint32_t clock_source = 0U;
  UART_GETCLOCKSOURCE(uart_handle, clock_source);
  return HAL_RCCEx_GetPeriphCLKFreq(clock_source);
#else
  UART_ClockSourceTypeDef clock_source{};
  UART_GETCLOCKSOURCE(uart_handle, clock_source);

#if defined(RCC_D2CFGR_D2PPRE1)
  switch (clock_source)
  {
    case UART_CLOCKSOURCE_D2PCLK1:
      return HAL_RCC_GetPCLK1Freq();
    case UART_CLOCKSOURCE_D2PCLK2:
      return HAL_RCC_GetPCLK2Freq();
    case UART_CLOCKSOURCE_D3PCLK1:
      return HAL_RCCEx_GetD3PCLK1Freq();
    case UART_CLOCKSOURCE_PLL2:
    {
      PLL2_ClocksTypeDef clocks{};
      HAL_RCCEx_GetPLL2ClockFreq(&clocks);
      return clocks.PLL2_Q_Frequency;
    }
    case UART_CLOCKSOURCE_PLL3:
    {
      PLL3_ClocksTypeDef clocks{};
      HAL_RCCEx_GetPLL3ClockFreq(&clocks);
      return clocks.PLL3_Q_Frequency;
    }
    case UART_CLOCKSOURCE_HSI:
      if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
      {
        return static_cast<uint32_t>(HSI_VALUE >> (__HAL_RCC_GET_HSI_DIVIDER() >> 3U));
      }
      return static_cast<uint32_t>(HSI_VALUE);
    case UART_CLOCKSOURCE_CSI:
      return static_cast<uint32_t>(CSI_VALUE);
    case UART_CLOCKSOURCE_LSE:
      return static_cast<uint32_t>(LSE_VALUE);
    default:
      return 0U;
  }
#else
  switch (clock_source)
  {
    case UART_CLOCKSOURCE_PCLK1:
      return HAL_RCC_GetPCLK1Freq();
#if defined(RCC_CFGR_PPRE2)
    case UART_CLOCKSOURCE_PCLK2:
      return HAL_RCC_GetPCLK2Freq();
#endif
    case UART_CLOCKSOURCE_HSI:
#if defined(__HAL_RCC_GET_HSIKER_DIVIDER) && defined(RCC_CR_HSIKERDIV_Pos)
      return static_cast<uint32_t>(
          HSI_VALUE / ((__HAL_RCC_GET_HSIKER_DIVIDER() >> RCC_CR_HSIKERDIV_Pos) + 1U));
#elif defined(RCC_CR_HSIDIVEN)
      return (__HAL_RCC_GET_FLAG(RCC_FLAG_HSIDIV) != 0U)
                 ? static_cast<uint32_t>(HSI_VALUE >> 2U)
                 : static_cast<uint32_t>(HSI_VALUE);
#else
      return static_cast<uint32_t>(HSI_VALUE);
#endif
    case UART_CLOCKSOURCE_SYSCLK:
      return HAL_RCC_GetSysClockFreq();
    case UART_CLOCKSOURCE_LSE:
      return static_cast<uint32_t>(LSE_VALUE);
    default:
      return 0U;
  }
#endif
#endif
#else
  bool uses_pclk2 = false;
#ifdef USART1
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == USART1);
#endif
#ifdef USART6
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == USART6);
#endif
#ifdef UART9
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == UART9);
#endif
#ifdef UART10
  uses_pclk2 = uses_pclk2 || (uart_handle->Instance == UART10);
#endif
  return uses_pclk2 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
#endif
}

[[nodiscard]] bool UartBaudrateSupported(UART_HandleTypeDef* uart_handle,
                                         uint32_t baudrate)
{
  constexpr uint32_t uart_brr_min = 0x10U;
  constexpr uint32_t uart_brr_max = 0xFFFFU;

  if ((baudrate == 0U) || (IS_UART_BAUDRATE(baudrate) == 0U))
  {
    return false;
  }

  const uint32_t kernel_clock = GetUartKernelClock(uart_handle);
  if (kernel_clock == 0U)
  {
    return false;
  }

#if defined(UART_PRESCALER_DIV1)
  if (IS_UART_PRESCALER(uart_handle->Init.ClockPrescaler) == 0U)
  {
    return false;
  }
#endif

#if defined(UART_INSTANCE_LOWPOWER)
  if (UART_INSTANCE_LOWPOWER(uart_handle))
  {
    constexpr uint32_t lpuart_brr_min = 0x300U;
    constexpr uint32_t lpuart_brr_max = 0xFFFFFU;
#if defined(UART_PRESCALER_DIV1)
    const uint32_t prescaler = UARTPrescTable[uart_handle->Init.ClockPrescaler];
    const uint32_t prescaled_clock = kernel_clock / prescaler;
    const uint32_t divisor =
        UART_DIV_LPUART(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    const uint32_t prescaled_clock = kernel_clock;
    const uint32_t divisor = UART_DIV_LPUART(kernel_clock, baudrate);
#endif
    const uint64_t baudrate_wide = baudrate;
    return (static_cast<uint64_t>(prescaled_clock) >= (3U * baudrate_wide)) &&
           (static_cast<uint64_t>(prescaled_clock) <= (4096U * baudrate_wide)) &&
           (divisor >= lpuart_brr_min) && (divisor <= lpuart_brr_max);
  }
#endif

#if defined(UART_GETCLOCKSOURCE)
  uint32_t divisor = 0U;
  if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_8)
  {
#if defined(UART_PRESCALER_DIV1)
    divisor =
        UART_DIV_SAMPLING8(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    divisor = UART_DIV_SAMPLING8(kernel_clock, baudrate);
#endif
  }
  else if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_16)
  {
#if defined(UART_PRESCALER_DIV1)
    divisor =
        UART_DIV_SAMPLING16(kernel_clock, baudrate, uart_handle->Init.ClockPrescaler);
#else
    divisor = UART_DIV_SAMPLING16(kernel_clock, baudrate);
#endif
  }
  return (divisor >= uart_brr_min) && (divisor <= uart_brr_max);
#else
  uint32_t brr = 0U;
#if defined(USART_CR1_OVER8)
  if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_8)
  {
    brr = UART_BRR_SAMPLING8(kernel_clock, baudrate);
  }
  else if (uart_handle->Init.OverSampling == UART_OVERSAMPLING_16)
#endif
  {
    brr = UART_BRR_SAMPLING16(kernel_clock, baudrate);
  }
  return (brr >= uart_brr_min) && (brr <= uart_brr_max);
#endif
}

STM32UART* FindRegisteredUart(UART_HandleTypeDef* uart_handle)
{
  if (uart_handle == nullptr)
  {
    return nullptr;
  }

  const stm32_uart_id_t id = stm32_uart_get_id(uart_handle->Instance);
  if ((id == STM32_UART_ID_ERROR) ||
      (static_cast<size_t>(id) >= static_cast<size_t>(STM32_UART_NUMBER)))
  {
    return nullptr;
  }

  STM32UART* const uart = STM32UART::map[id];
  return ((uart != nullptr) && (uart->uart_handle_ == uart_handle)) ? uart : nullptr;
}
}  // namespace

stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr)
{
  if (addr == nullptr)
  {
    return STM32_UART_ID_ERROR;
  }
#ifdef USART1
  if (addr == USART1) return STM32_USART1;
#endif
#ifdef USART2
  if (addr == USART2) return STM32_USART2;
#endif
#ifdef USART3
  if (addr == USART3) return STM32_USART3;
#endif
#ifdef USART4
  if (addr == USART4) return STM32_USART4;
#endif
#ifdef USART5
  if (addr == USART5) return STM32_USART5;
#endif
#ifdef USART6
  if (addr == USART6) return STM32_USART6;
#endif
#ifdef USART7
  if (addr == USART7) return STM32_USART7;
#endif
#ifdef USART8
  if (addr == USART8) return STM32_USART8;
#endif
#ifdef USART9
  if (addr == USART9) return STM32_USART9;
#endif
#ifdef USART10
  if (addr == USART10) return STM32_USART10;
#endif
#ifdef USART11
  if (addr == USART11) return STM32_USART11;
#endif
#ifdef USART12
  if (addr == USART12) return STM32_USART12;
#endif
#ifdef USART13
  if (addr == USART13) return STM32_USART13;
#endif
#ifdef UART1
  if (addr == UART1) return STM32_UART1;
#endif
#ifdef UART2
  if (addr == UART2) return STM32_UART2;
#endif
#ifdef UART3
  if (addr == UART3) return STM32_UART3;
#endif
#ifdef UART4
  if (addr == UART4) return STM32_UART4;
#endif
#ifdef UART5
  if (addr == UART5) return STM32_UART5;
#endif
#ifdef UART6
  if (addr == UART6) return STM32_UART6;
#endif
#ifdef UART7
  if (addr == UART7) return STM32_UART7;
#endif
#ifdef UART8
  if (addr == UART8) return STM32_UART8;
#endif
#ifdef UART9
  if (addr == UART9) return STM32_UART9;
#endif
#ifdef UART10
  if (addr == UART10) return STM32_UART10;
#endif
#ifdef UART11
  if (addr == UART11) return STM32_UART11;
#endif
#ifdef UART12
  if (addr == UART12) return STM32_UART12;
#endif
#ifdef UART13
  if (addr == UART13) return STM32_UART13;
#endif
#ifdef LPUART1
  if (addr == LPUART1) return STM32_LPUART1;
#endif
#ifdef LPUART2
  if (addr == LPUART2) return STM32_LPUART2;
#endif
#ifdef LPUART3
  if (addr == LPUART3) return STM32_LPUART3;
#endif
  return STM32_UART_ID_ERROR;
}

bool STM32UART::InIsr()
{
#if defined(__CORTEX_M)
  return __get_IPSR() != 0U;
#else
  return false;
#endif
}

bool STM32UART::DmaTransferSizeSupported(size_t size)
{
  if ((size == 0U) || (size > UINT16_MAX))
  {
    return false;
  }
#if defined(IS_DMA_BUFFER_SIZE)
  return IS_DMA_BUFFER_SIZE(static_cast<uint32_t>(size));
#else
  return true;
#endif
}

bool STM32UART::IsPendingRxLineError(uint32_t error_code)
{
  uint32_t line_error_mask = 0U;
#ifdef HAL_UART_ERROR_PE
  line_error_mask |= HAL_UART_ERROR_PE;
#endif
#ifdef HAL_UART_ERROR_NE
  line_error_mask |= HAL_UART_ERROR_NE;
#endif
#ifdef HAL_UART_ERROR_FE
  line_error_mask |= HAL_UART_ERROR_FE;
#endif
#ifdef HAL_UART_ERROR_ORE
  line_error_mask |= HAL_UART_ERROR_ORE;
#endif
#ifdef HAL_UART_ERROR_RTO
  line_error_mask |= HAL_UART_ERROR_RTO;
#endif

  if ((error_code & line_error_mask) == 0U)
  {
    return false;
  }
#ifdef HAL_UART_ERROR_DMA
  if ((error_code & HAL_UART_ERROR_DMA) != 0U)
  {
    return false;
  }
#endif
  return true;
}

ErrorCode STM32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &STM32UART::_write_port);
  return uart->dma_model_.Submit(in_isr);
}

ErrorCode STM32UART::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

STM32UART::STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx,
                     RawData dma_buff_tx, uint32_t tx_queue_size)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2U),
      rx_dma_model_(dma_buff_rx),
      dma_model_(*this, execution_policy_, _write_port, dma_buff_tx),
      uart_handle_(uart_handle),
      id_(stm32_uart_get_id((uart_handle == nullptr) ? nullptr : uart_handle->Instance))
{
  REQUIRE(uart_handle_ != nullptr);
  REQUIRE(id_ != STM32_UART_ID_ERROR);
  REQUIRE(map[id_] == nullptr);
  REQUIRE((uart_handle_->hdmatx == nullptr) || (uart_handle_->hdmarx == nullptr) ||
          (uart_handle_->hdmatx != uart_handle_->hdmarx));
  map[id_] = this;

  if ((uart_handle_->Init.Mode & UART_MODE_TX) == UART_MODE_TX)
  {
    REQUIRE(uart_handle_->hdmatx != nullptr);
    REQUIRE(dma_buff_tx.addr_ != nullptr);
    REQUIRE((reinterpret_cast<uintptr_t>(dma_buff_tx.addr_) % alignof(size_t)) == 0U);
    REQUIRE((dma_buff_tx.size_ % (2U * alignof(size_t))) == 0U);
    REQUIRE(DmaTransferSizeSupported(dma_buff_tx.size_ / 2U));
    _write_port = WriteFun;
  }
  if ((uart_handle_->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
  {
    REQUIRE(uart_handle_->hdmarx != nullptr);
    REQUIRE(DmaTransferSizeSupported(dma_buff_rx.size_));
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    REQUIRE((reinterpret_cast<uintptr_t>(dma_buff_rx.addr_) % HW_CACHE_LINE_SIZE) == 0U);
    REQUIRE((dma_buff_rx.size_ % HW_CACHE_LINE_SIZE) == 0U);
#endif
    _read_port = ReadFun;
  }
  SetRxDMA();
}

ErrorCode STM32UART::SetConfig(UART::Configuration config)
{
  return dma_model_.SetConfig(config, InIsr());
}

ErrorCode STM32UART::ValidateConfig(UART::Configuration config) const
{
  if (!UartBaudrateSupported(uart_handle_, config.baudrate) || (config.data_bits != 8U) ||
      ((config.stop_bits != 1U) && (config.stop_bits != 2U)))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.parity != UART::Parity::NO_PARITY) &&
      (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

UartDmaControlResult STM32UART::AdvanceConfig(UART::Configuration config, bool active_tx,
                                              bool in_isr)
{
  if (!active_tx)
  {
    tx_replay_required_ = false;
  }

  UartDmaControlResult stop_result = StopDataPath(active_tx, true, in_isr);
  if (!stop_result.IsCompleted())
  {
    return stop_result;
  }

  // Recovery may have stopped an error-bearing active generation before CONFIG won
  // admission. HAL cleanup preserves NDTR but not the DMA error flag, so a later CONFIG
  // must not reinterpret NDTR == 0 as a successful completion of that same generation.
  stop_result.old_tx_terminal =
      PreserveRequiredReplay(tx_replay_required_, stop_result.old_tx_terminal);

  const bool configured = ApplyConfigPayload(config, in_isr);
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  return stop_result;
}

UartDmaControlProgress STM32UART::CompleteConfig(bool in_isr)
{
  if (SetRxDMA(in_isr) == UartDmaControlProgress::PENDING)
  {
    return UartDmaControlProgress::PENDING;
  }
  FinishControl();
  return UartDmaControlProgress::COMPLETED;
}

UartDmaControlResult STM32UART::AdvanceRecovery(bool active_tx, bool in_isr)
{
  if (!active_tx)
  {
    tx_replay_required_ = false;
  }

  const UartDmaControlResult stop_result = StopDataPath(active_tx, false, in_isr);
  if (stop_result.IsCompleted())
  {
    tx_replay_required_ =
        active_tx && (stop_result.old_tx_terminal != UartOldTxTerminal::COMPLETE);
  }
  return stop_result;
}

UartDmaControlProgress STM32UART::CompleteRecovery(bool in_isr)
{
  if (SetRxDMA(in_isr) == UartDmaControlProgress::PENDING)
  {
    return UartDmaControlProgress::PENDING;
  }
  FinishControl();
  return UartDmaControlProgress::COMPLETED;
}

UartDmaControlResult STM32UART::StopDataPath(bool active_tx, bool wait_for_uart_tc,
                                             bool in_isr)
{
  if (!stop_active_)
  {
    stop_active_ = true;
    tx_evidence_captured_ = false;
    tx_payload_complete_ = false;
    tx_dma_error_ = false;
    waiting_for_uart_tc_ = false;
    CloseTxTerminalSource();
    if (uart_handle_->hdmatx != nullptr)
    {
      LaunchDmaStop(uart_handle_->hdmatx, in_isr, active_tx);
    }
    if (uart_handle_->hdmarx != nullptr)
    {
      LaunchDmaStop(uart_handle_->hdmarx, in_isr, false);
    }
  }

  if (!AllDmaStopsComplete())
  {
    return UartDmaControlResult::Pending();
  }

  UartOldTxTerminal old_tx_terminal = UartOldTxTerminal::NONE;
  if (active_tx && (uart_handle_->hdmatx != nullptr))
  {
    TraditionalDmaAdapter::CaptureStoppedTx(uart_handle_->hdmatx, tx_evidence_captured_,
                                            tx_payload_complete_, tx_dma_error_);
  }

  // A recovery stop may complete before a reserved CONFIG takes over. Its captured
  // active evidence still represents the old line generation even if common already
  // consumed a COMPLETE carrier and cleared active_length_.
  const bool stopped_active_tx = active_tx || tx_evidence_captured_;
  if (wait_for_uart_tc && stopped_active_tx &&
      ((uart_handle_->Init.Mode & UART_MODE_TX) == UART_MODE_TX))
  {
    if (waiting_for_uart_tc_)
    {
      // UART_EndTransmit_IT() disables TCIE before publishing the HAL callback.
      // While TCIE remains enabled, that callback has not retired this generation.
      if (__HAL_UART_GET_IT_SOURCE(uart_handle_, UART_IT_TC) != RESET)
      {
        return UartDmaControlResult::Pending();
      }
    }
    else if (__HAL_UART_GET_FLAG(uart_handle_, UART_FLAG_TC) == RESET)
    {
      // DMA has stopped, but the UART shifter has not drained. Publish the wait state
      // before enabling TCIE so immediate preemption is safe. The TC callback below
      // publishes COMPLETE only for a complete/error-free active payload; otherwise it
      // publishes STOP_DONE.
      waiting_for_uart_tc_ = true;
      ATOMIC_SET_BIT(uart_handle_->Instance->CR1, USART_CR1_TCIE);
      const volatile uint32_t cr1 = uart_handle_->Instance->CR1;
      UNUSED(cr1);
      __DSB();
      return UartDmaControlResult::Pending();
    }
  }

  if (active_tx && (uart_handle_->hdmatx != nullptr))
  {
    if (tx_dma_error_)
    {
      old_tx_terminal = UartOldTxTerminal::ERROR;
    }
    else if (tx_payload_complete_)
    {
      // CONFIG reaches this classification only after TC. Runtime ERROR recovery does
      // not wait, so DMA completion alone retires the old generation there.
      old_tx_terminal = UartOldTxTerminal::COMPLETE;
    }
  }

  // With both DMA handles already READY, HAL's blocking abort can only take the
  // immediate NO_XFER path; it still performs the UART-side cleanup and state reset.
  const bool aborted = HAL_UART_Abort(uart_handle_) == HAL_OK;
  REQUIRE_FROM_CALLBACK(aborted, in_isr);
  DEV_ASSERT_FROM_CALLBACK(uart_handle_->gState == HAL_UART_STATE_READY, in_isr);
  DEV_ASSERT_FROM_CALLBACK(uart_handle_->RxState == HAL_UART_STATE_READY, in_isr);
  DEV_ASSERT_FROM_CALLBACK(uart_handle_->ErrorCode == HAL_UART_ERROR_NONE, in_isr);
  DEV_ASSERT_FROM_CALLBACK(uart_handle_->Lock == HAL_UNLOCKED, in_isr);
  if (uart_handle_->hdmatx != nullptr)
  {
    TraditionalDmaAdapter::FinalizeStopped(uart_handle_->hdmatx, in_isr);
  }
  if (uart_handle_->hdmarx != nullptr)
  {
    TraditionalDmaAdapter::FinalizeStopped(uart_handle_->hdmarx, in_isr);
  }

#if !defined(UART_CLEAR_OREF) && defined(__HAL_UART_CLEAR_PEFLAG)
  __HAL_UART_CLEAR_PEFLAG(uart_handle_);
#endif
  return UartDmaControlResult::Completed(old_tx_terminal);
}

void STM32UART::FinishControl()
{
  ASSERT(stop_active_);
  stop_active_ = false;
  tx_evidence_captured_ = false;
  tx_payload_complete_ = false;
  tx_dma_error_ = false;
  waiting_for_uart_tc_ = false;
}

bool STM32UART::ApplyConfigPayload(UART::Configuration config, bool in_isr)
{
  uart_handle_->Init.BaudRate = config.baudrate;
  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      uart_handle_->Init.Parity = UART_PARITY_NONE;
      uart_handle_->Init.WordLength = UART_WORDLENGTH_8B;
      break;
    case UART::Parity::EVEN:
      uart_handle_->Init.Parity = UART_PARITY_EVEN;
      uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
      break;
    case UART::Parity::ODD:
      uart_handle_->Init.Parity = UART_PARITY_ODD;
      uart_handle_->Init.WordLength = UART_WORDLENGTH_9B;
      break;
    default:
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return false;
  }
  uart_handle_->Init.StopBits =
      (config.stop_bits == 2U) ? UART_STOPBITS_2 : UART_STOPBITS_1;

  __HAL_UART_DISABLE(uart_handle_);
#if defined(USART_ISR_TEACK) || defined(USART_ISR_REACK)
  bool configured = UART_SetConfig(uart_handle_) == HAL_OK;
#if defined(USART_CR2_LINEN) && defined(USART_CR2_CLKEN)
  CLEAR_BIT(uart_handle_->Instance->CR2, USART_CR2_LINEN | USART_CR2_CLKEN);
#endif
#if defined(USART_CR3_SCEN) && defined(USART_CR3_HDSEL) && defined(USART_CR3_IREN)
  CLEAR_BIT(uart_handle_->Instance->CR3,
            USART_CR3_SCEN | USART_CR3_HDSEL | USART_CR3_IREN);
#endif
  if (configured)
  {
    __HAL_UART_ENABLE(uart_handle_);
  }
#else
  bool configured = HAL_UART_Init(uart_handle_) == HAL_OK;
#endif
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  if (!configured)
  {
    return false;
  }

  return true;
}

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

void STM32UART::DmaAbortCallback(DMA_HandleTypeDef* dma_handle)
{
  if (dma_handle == nullptr)
  {
    return;
  }
  auto* uart = FindRegisteredUart(static_cast<UART_HandleTypeDef*>(dma_handle->Parent));
  if (uart != nullptr)
  {
    uart->dma_model_.OnStopDone(InIsr());
  }
}

void STM32UART::SetRxDMA()
{
  const bool in_isr = InIsr();
  const UartDmaControlProgress result = SetRxDMA(in_isr);
  // A pending line error already owns its HAL error/abort completion carrier.
  REQUIRE_FROM_CALLBACK((result == UartDmaControlProgress::COMPLETED) ||
                            (rx_arm_result_ == RxArmResult::PENDING_LINE_ERROR),
                        in_isr);
}

UartDmaControlProgress STM32UART::SetRxDMA(bool in_isr)
{
  if ((uart_handle_->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
  {
    ASSERT(uart_handle_->hdmarx != nullptr);
    rx_dma_model_.Start(*this);
    REQUIRE_FROM_CALLBACK(rx_arm_result_ != RxArmResult::FAILED, in_isr);
    if (rx_arm_result_ == RxArmResult::PENDING_LINE_ERROR)
    {
      return UartDmaControlProgress::PENDING;
    }
  }
  return UartDmaControlProgress::COMPLETED;
}

void STM32UART::OnRxDataAvailable(bool in_isr)
{
  bool data_available = false;
  const bool admitted = dma_model_.ProcessRx(
      in_isr, [this, &data_available]()
      { data_available = rx_dma_model_.OnDataAvailable(*this, _read_port); });
  if (!admitted)
  {
    return;
  }
  if (data_available)
  {
    _read_port.ProcessPendingReads(in_isr);
  }
}

void STM32UART::OnTxComplete(bool in_isr)
{
  if (ShouldPublishStopDoneOnTxComplete(stop_active_, waiting_for_uart_tc_))
  {
    dma_model_.OnStopDone(in_isr);
    return;
  }
  dma_model_.OnTransferDone(in_isr);
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->OnRxDataAvailable(STM32UART::InIsr());
  }
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->OnTxComplete(STM32UART::InIsr());
  }
}

extern "C" __attribute__((used)) void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->dma_model_.OnTransferError(STM32UART::InIsr());
  }
}

UartDmaTxStartResult STM32UART::StartDmaTx(uint8_t* data, size_t size, int, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(DmaTransferSizeSupported(size), in_isr);
  STM32_CleanDCacheByAddr(data, size);
  if (HAL_UART_Transmit_DMA(uart_handle_, data, static_cast<uint16_t>(size)) != HAL_OK)
  {
    return UartDmaTxStartResult::FAILED;
  }
  tx_replay_required_ = false;
  return UartDmaTxStartResult::STARTED;
}

#endif
