#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

STM32UART* STM32UART::map[STM32_UART_NUMBER] = {nullptr};

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
                                       AbortCallback callback)
  {
    const auto launch = [&]()
    {
      if (StopComplete(dma_handle))
      {
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

      restore_ = NVIC_GetEnableIRQ(irq_) != 0U;
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

ErrorCode STM32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &STM32UART::_write_port);
  return uart->tx_dma_model_.Submit(uart->execution_policy_, in_isr);
}

ErrorCode STM32UART::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

static UART::Configuration GetConfig(UART_HandleTypeDef* handle)
{
  UART::Parity parity = UART::Parity::NO_PARITY;
  if (handle->Init.Parity == UART_PARITY_EVEN)
  {
    parity = UART::Parity::EVEN;
  }
  else if (handle->Init.Parity == UART_PARITY_ODD)
  {
    parity = UART::Parity::ODD;
  }
  return UART::Configuration{
      handle->Init.BaudRate, parity, 8U,
      static_cast<uint8_t>(handle->Init.StopBits == UART_STOPBITS_2 ? 2U : 1U)};
}

STM32UART::STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx,
                     RawData dma_buff_tx, uint32_t tx_queue_size)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2U),
      requested_config_(GetConfig(uart_handle)),
      rx_dma_model_(dma_buff_rx),
      tx_dma_model_(*this, _write_port, dma_buff_tx),
      uart_handle_(uart_handle),
      id_(stm32_uart_get_id(uart_handle_->Instance))
{
  ASSERT(id_ != STM32_UART_ID_ERROR);
  ASSERT((uart_handle_->hdmatx == nullptr) || (uart_handle_->hdmarx == nullptr) ||
         (uart_handle_->hdmatx != uart_handle_->hdmarx));
  map[id_] = this;

  if ((uart_handle_->Init.Mode & UART_MODE_TX) == UART_MODE_TX)
  {
    ASSERT(uart_handle_->hdmatx != nullptr);
    _write_port = WriteFun;
  }
  SetRxDMA();
}

ErrorCode STM32UART::SetConfig(UART::Configuration config)
{
  if ((config.baudrate == 0U) || ((config.stop_bits != 1U) && (config.stop_bits != 2U)))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.parity != UART::Parity::NO_PARITY) &&
      (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }

  if (!rx_config_gate_.TryReserveConfig())
  {
    return ErrorCode::BUSY;
  }
  requested_config_ = config;
  rx_config_gate_.PublishConfig();
  tx_dma_model_.RequestConfig(execution_policy_, InIsr());
  return ErrorCode::OK;
}

UartDmaControlResult STM32UART::ApplyPendingConfig(bool in_isr)
{
  if (!rx_config_gate_.TryEnterConfig())
  {
    return UartDmaControlResult::PENDING;
  }

  if (StopDataPath(in_isr) == UartDmaControlResult::PENDING)
  {
    return UartDmaControlResult::PENDING;
  }

  const bool configured = ApplyConfigPayload(in_isr);
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  if (configured)
  {
    SetRxDMA();
  }
  FinishControl();
  return UartDmaControlResult::COMPLETED;
}

UartDmaControlResult STM32UART::RecoverDataPath(bool in_isr)
{
  if (!rx_config_gate_.TryEnterRecovery())
  {
    return UartDmaControlResult::PENDING;
  }

  if (StopDataPath(in_isr) == UartDmaControlResult::PENDING)
  {
    return UartDmaControlResult::PENDING;
  }

  SetRxDMA();
  FinishControl();
  rx_config_gate_.LeaveRecovery();
  return UartDmaControlResult::COMPLETED;
}

UartDmaControlResult STM32UART::StopDataPath(bool in_isr)
{
  if (!stop_active_)
  {
    stop_active_ = true;
    if (uart_handle_->hdmatx != nullptr) LaunchDmaStop(uart_handle_->hdmatx, in_isr);
    if (uart_handle_->hdmarx != nullptr) LaunchDmaStop(uart_handle_->hdmarx, in_isr);
  }

  if (!AllDmaStopsComplete())
  {
    return UartDmaControlResult::PENDING;
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
  return UartDmaControlResult::COMPLETED;
}

void STM32UART::FinishControl()
{
  ASSERT(stop_active_);
  stop_active_ = false;
}

bool STM32UART::ApplyConfigPayload(bool in_isr)
{
  const UART::Configuration config = requested_config_;

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

void STM32UART::LaunchDmaStop(DMA_HandleTypeDef* dma_handle, bool in_isr)
{
  ASSERT(dma_handle != nullptr);
  ASSERT(dma_handle->Parent == uart_handle_);
  const bool accepted = TraditionalDmaAdapter::LaunchStop(dma_handle, DmaAbortCallback);
  REQUIRE_FROM_CALLBACK(accepted, in_isr);
}

void STM32UART::DmaAbortCallback(DMA_HandleTypeDef* dma_handle)
{
  ASSERT(dma_handle != nullptr);
  auto* uart_handle = static_cast<UART_HandleTypeDef*>(dma_handle->Parent);
  ASSERT(uart_handle != nullptr);
  auto* uart = map[stm32_uart_get_id(uart_handle->Instance)];
  ASSERT(uart != nullptr);
  if (uart != nullptr)
  {
    uart->tx_dma_model_.OnStopDone(uart->execution_policy_, InIsr());
  }
}

void STM32UART::SetRxDMA()
{
  if ((uart_handle_->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
  {
    ASSERT(uart_handle_->hdmarx != nullptr);
    rx_dma_model_.Start(*this);
    _read_port = ReadFun;
  }
}

void STM32UART::OnRxDataAvailable(bool in_isr)
{
  if (!rx_config_gate_.TryEnterRx())
  {
    tx_dma_model_.OnControlReady(execution_policy_, in_isr);
    return;
  }

  const bool data_available = rx_dma_model_.OnDataAvailable(*this, _read_port);
  const bool resume_control = rx_config_gate_.LeaveRx();
  if (resume_control)
  {
    tx_dma_model_.OnControlReady(execution_policy_, in_isr);
  }
  if (data_available)
  {
    _read_port.ProcessPendingReads(in_isr);
  }
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  uart->OnRxDataAvailable(STM32UART::InIsr());
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  uart->tx_dma_model_.OnTransferDone(uart->execution_policy_, STM32UART::InIsr());
}

extern "C" __attribute__((used)) void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  uart->tx_dma_model_.OnTransferError(uart->execution_policy_, STM32UART::InIsr());
}

UartDmaTxStartResult STM32UART::StartDmaTx(uint8_t* data, size_t size, int)
{
  STM32_CleanDCacheByAddr(data, size);
  if (HAL_UART_Transmit_DMA(uart_handle_, data, size) != HAL_OK)
  {
    return UartDmaTxStartResult::FAILED;
  }
  return UartDmaTxStartResult::STARTED;
}

#endif
