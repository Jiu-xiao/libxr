#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

STM32UART* STM32UART::map[STM32_UART_NUMBER] = {nullptr};

namespace
{
bool STM32_UART_InIsr()
{
#if defined(__CORTEX_M)
  return __get_IPSR() != 0U;
#else
  return false;
#endif
}

bool STM32_DMA_IsAsyncStream(DMA_HandleTypeDef* dma_handle)
{
#if defined(DMA_SxCR_EN)
#if defined(IS_DMA_STREAM_INSTANCE)
  return IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U;
#else
  (void)dma_handle;
  return true;
#endif
#else
  (void)dma_handle;
  return false;
#endif
}

[[maybe_unused]] bool STM32_DMA_HasDmamux(DMA_HandleTypeDef* dma_handle)
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

void STM32_DMA_DisableDmamuxInterrupts(DMA_HandleTypeDef* dma_handle)
{
#if defined(DMAMUX_CxCR_SOIE)
  if (STM32_DMA_HasDmamux(dma_handle) && (dma_handle->DMAmuxChannel != nullptr))
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

void STM32_DMA_ClearDmamuxFlags(DMA_HandleTypeDef* dma_handle)
{
#if defined(DMAMUX_CxCR_SOIE)
  if (STM32_DMA_HasDmamux(dma_handle) && (dma_handle->DMAmuxChannelStatus != nullptr))
  {
    dma_handle->DMAmuxChannelStatus->CFR = dma_handle->DMAmuxChannelStatusMask;
#if defined(DMAMUX_RGxCR_OIE)
    if ((dma_handle->DMAmuxRequestGen != nullptr) &&
        (dma_handle->DMAmuxRequestGenStatus != nullptr))
    {
      dma_handle->DMAmuxRequestGenStatus->RGCFR = dma_handle->DMAmuxRequestGenStatusMask;
    }
#endif
  }
#else
  (void)dma_handle;
#endif
}

}  // namespace

STM32UART::CallbackScope::CallbackScope(STM32UART& uart,
                                        UartHardwareGate::OwnerContext* context,
                                        CallbackDispatch dispatch, DMA_HandleTypeDef* dma)
    : uart_(uart)
{
  ASSERT(uart_.callback_dispatch_ == CallbackDispatch::NONE);
  ASSERT(uart_.callback_context_ == nullptr);
  ASSERT(uart_.callback_dma_ == nullptr);
  ASSERT(dispatch != CallbackDispatch::NONE);

  uart_.callback_context_ = context;
  uart_.callback_dispatch_ = dispatch;
  uart_.callback_dma_ = dma;
}

STM32UART::CallbackScope::~CallbackScope()
{
  uart_.callback_dma_ = nullptr;
  uart_.callback_dispatch_ = CallbackDispatch::NONE;
  uart_.callback_context_ = nullptr;
}

stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
#ifdef USART1
  else if (addr == USART1)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART1;
  }
#endif
#ifdef USART2
  else if (addr == USART2)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART2;
  }
#endif
#ifdef USART3
  else if (addr == USART3)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART3;
  }
#endif
#ifdef USART4
  else if (addr == USART4)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART4;
  }
#endif
#ifdef USART5
  else if (addr == USART5)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART5;
  }
#endif
#ifdef USART6
  else if (addr == USART6)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART6;
  }
#endif
#ifdef USART7
  else if (addr == USART7)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART7;
  }
#endif
#ifdef USART8
  else if (addr == USART8)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART8;
  }
#endif
#ifdef USART9
  else if (addr == USART9)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART9;
  }
#endif
#ifdef USART10
  else if (addr == USART10)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART10;
  }
#endif
#ifdef USART11
  else if (addr == USART11)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART11;
  }
#endif
#ifdef USART12
  else if (addr == USART12)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART12;
  }
#endif
#ifdef USART13
  else if (addr == USART13)  // NOLINT
  {
    return stm32_uart_id_t::STM32_USART13;
  }
#endif
#ifdef UART1
  else if (addr == UART1)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART1;
  }
#endif
#ifdef UART2
  else if (addr == UART2)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART2;
  }
#endif
#ifdef UART3
  else if (addr == UART3)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART3;
  }
#endif
#ifdef UART4
  else if (addr == UART4)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART4;
  }
#endif
#ifdef UART5
  else if (addr == UART5)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART5;
  }
#endif
#ifdef UART6
  else if (addr == UART6)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART6;
  }
#endif
#ifdef UART7
  else if (addr == UART7)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART7;
  }
#endif
#ifdef UART8
  else if (addr == UART8)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART8;
  }
#endif
#ifdef UART9
  else if (addr == UART9)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART9;
  }
#endif
#ifdef UART10
  else if (addr == UART10)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART10;
  }
#endif
#ifdef UART11
  else if (addr == UART11)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART11;
  }
#endif
#ifdef UART12
  else if (addr == UART12)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART12;
  }
#endif
#ifdef UART13
  else if (addr == UART13)  // NOLINT
  {
    return stm32_uart_id_t::STM32_UART13;
  }
#endif
#ifdef LPUART1
  else if (addr == LPUART1)  // NOLINT
  {
    return stm32_uart_id_t::STM32_LPUART1;
  }
#endif
#ifdef LPUART2
  else if (addr == LPUART2)  // NOLINT
  {
    return stm32_uart_id_t::STM32_LPUART2;
  }
#endif
#ifdef LPUART3
  else if (addr == LPUART3)  // NOLINT
  {
    return stm32_uart_id_t::STM32_LPUART3;
  }
#endif
  else
  {
    return stm32_uart_id_t::STM32_UART_ID_ERROR;
  }
}

ErrorCode STM32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &STM32UART::_write_port);
  return uart->tx_dma_model_.Submit(in_isr);
}

ErrorCode STM32UART::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

static UART::Configuration STM32_UART_GetConfig(UART_HandleTypeDef* handle)
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
                     RawData dma_buff_tx, STM32UartIrqDomainOps irq_domain_ops,
                     uint32_t tx_queue_size)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2),
      requested_config_(STM32_UART_GetConfig(uart_handle)),
      rx_dma_model_(dma_buff_rx),
      tx_dma_model_(*this, _write_port, dma_buff_tx),
      uart_handle_(uart_handle),
      id_(stm32_uart_get_id(uart_handle_->Instance)),
      irq_domain_ops_(irq_domain_ops)
{
  ASSERT(id_ != STM32_UART_ID_ERROR);
  ASSERT(irq_domain_ops_.IsValid());
  ASSERT((uart_handle_->hdmatx == nullptr) || (uart_handle_->hdmarx == nullptr) ||
         (uart_handle_->hdmatx != uart_handle_->hdmarx));

  map[id_] = this;

  if ((uart_handle->Init.Mode & UART_MODE_TX) == UART_MODE_TX)
  {
    ASSERT(uart_handle_->hdmatx != NULL);
    _write_port = WriteFun;
  }

  SetRxDMA();
}

ErrorCode STM32UART::SetConfig(UART::Configuration config)
{
  if (config.baudrate == 0U)
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.stop_bits != 1U) && (config.stop_bits != 2U))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.parity != UART::Parity::NO_PARITY) &&
      (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }

  requested_config_.Store(config);
  tx_dma_model_.RequestConfig(STM32_UART_InIsr());
  return ErrorCode::OK;
}

bool STM32UART::ApplyPendingConfig(bool in_isr)
{
  using AbortDirection = UartDmaAbortJoin::Direction;
  using AbortPhase = UartDmaAbortJoin::Phase;

  AbortPhase phase = abort_join_.GetPhase();
  if (phase == AbortPhase::IDLE)
  {
    if (!hardware_gate_.TryEnterConfig())
    {
      return false;
    }

    rx_admission_.store(0U, std::memory_order_release);
    const uint32_t abort_mask =
        ((uart_handle_->hdmatx != nullptr) ? UartDmaAbortJoin::Mask(AbortDirection::TX)
                                           : 0U) |
        ((uart_handle_->hdmarx != nullptr) ? UartDmaAbortJoin::Mask(AbortDirection::RX)
                                           : 0U);

    // Keep DMAT/DMAR requests enabled until each DMA has physically stopped. Disable
    // every ordinary peripheral source and order/read back those writes before making
    // the abort generation visible. Stream flags are cleared only after EN-clear has
    // been requested; a second EN check then closes the clear-versus-stop race.
    DisableUartInterrupts();
    if (uart_handle_->hdmatx != nullptr)
    {
      DisableDmaInterrupts(uart_handle_->hdmatx);
    }
    if (uart_handle_->hdmarx != nullptr)
    {
      DisableDmaInterrupts(uart_handle_->hdmarx);
    }
    SynchronizeDisabledInterrupts();

    abort_join_.Begin(abort_mask);

    bool admit_tx_abort_irq = false;
    bool admit_rx_abort_irq = false;
    if ((abort_mask & UartDmaAbortJoin::Mask(AbortDirection::TX)) != 0U)
    {
      admit_tx_abort_irq =
          LaunchDmaAbort(uart_handle_->hdmatx, AbortDirection::TX, in_isr);
    }
    if ((abort_mask & UartDmaAbortJoin::Mask(AbortDirection::RX)) != 0U)
    {
      admit_rx_abort_irq =
          LaunchDmaAbort(uart_handle_->hdmarx, AbortDirection::RX, in_isr);
    }

    // Arm asynchronous Stream stop IRQs only after both HAL calls have returned. Each
    // direction is quenched and checked again immediately before admission so a stop
    // that raced the two launch calls is completed without waiting for a new IRQ.
    if (admit_tx_abort_irq)
    {
      admit_tx_abort_irq = ArmAsyncStop(uart_handle_->hdmatx, AbortDirection::TX, in_isr);
    }
    if (admit_rx_abort_irq)
    {
      admit_rx_abort_irq = ArmAsyncStop(uart_handle_->hdmarx, AbortDirection::RX, in_isr);
    }

    (void)abort_join_.EndLaunch();
    if (admit_tx_abort_irq || admit_rx_abort_irq)
    {
      PublishIrqDomainRequest(IrqDomainRequest::RESTORE_NORMAL);
    }
    phase = abort_join_.GetPhase();
  }

  if ((phase == AbortPhase::LAUNCHING) || (phase == AbortPhase::STOPPING))
  {
    return false;
  }

  if (phase != AbortPhase::QUIESCENT)
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return false;
  }

  if (uart_handle_->hdmatx != nullptr)
  {
    FinalizeStoppedDma(uart_handle_->hdmatx, in_isr);
  }
  if (uart_handle_->hdmarx != nullptr)
  {
    FinalizeStoppedDma(uart_handle_->hdmarx, in_isr);
  }

  hardware_gate_.ConsumePendingConfig();
  if (!ApplyConfigPayload(in_isr))
  {
    return false;
  }
  SetRxDMA();
  abort_join_.MarkApplied();
  return true;
}

bool STM32UART::OnConfigApplied(bool in_isr)
{
  ASSERT(abort_join_.GetPhase() == UartDmaAbortJoin::Phase::APPLIED);

  rx_admission_.store(1U, std::memory_order_release);
  abort_join_.Finish();
  PublishIrqDomainRequest(IrqDomainRequest::RESTORE_NORMAL);
  DispatchHardwareActions(hardware_gate_.LeaveConfig(), in_isr);
  return true;
}

bool STM32UART::ApplyConfigPayload(bool in_isr)
{
  UART::Configuration config{};
  (void)requested_config_.LoadLatest(config);

#if defined(USART_CR3_DMAT) && defined(USART_CR3_DMAR)
  CLEAR_BIT(uart_handle_->Instance->CR3, USART_CR3_DMAT | USART_CR3_DMAR);
#endif

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

  switch (config.stop_bits)
  {
    case 1:
      uart_handle_->Init.StopBits = UART_STOPBITS_1;
      break;
    case 2:
      uart_handle_->Init.StopBits = UART_STOPBITS_2;
      break;
    default:
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return false;
  }

  __HAL_UART_DISABLE(uart_handle_);

#if defined(USART_ISR_TEACK) || defined(USART_ISR_REACK)
  const bool configured = UART_SetConfig(uart_handle_) == HAL_OK;
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  if (!configured)
  {
    return false;
  }

#if defined(USART_CR2_LINEN) && defined(USART_CR2_CLKEN)
  CLEAR_BIT(uart_handle_->Instance->CR2, USART_CR2_LINEN | USART_CR2_CLKEN);
#endif
#if defined(USART_CR3_SCEN) && defined(USART_CR3_HDSEL) && defined(USART_CR3_IREN)
  CLEAR_BIT(uart_handle_->Instance->CR3,
            USART_CR3_SCEN | USART_CR3_HDSEL | USART_CR3_IREN);
#endif

  __HAL_UART_ENABLE(uart_handle_);
  // UART_CheckIdleState() polls TEACK/REACK before publishing HAL READY state. CONFIG
  // may execute in an ISR, so publish the programmed state without waiting. STM32's
  // receive sequence explicitly allows DMAR before RE, and DMAR/DMAT have no ACK write
  // restriction; the kernel-clock/final-ACK requirement is part of the driver contract.
#else
  const bool configured = HAL_UART_Init(uart_handle_) == HAL_OK;
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  if (!configured)
  {
    return false;
  }
#endif

  uart_handle_->TxXferCount = 0U;
  uart_handle_->RxXferCount = 0U;
  uart_handle_->gState = HAL_UART_STATE_READY;
  uart_handle_->RxState = HAL_UART_STATE_READY;
  uart_handle_->ErrorCode = HAL_UART_ERROR_NONE;
  uart_handle_->Lock = HAL_UNLOCKED;
#ifdef HAL_UART_RECEPTION_STANDARD
  uart_handle_->ReceptionType = HAL_UART_RECEPTION_STANDARD;
#endif
#ifdef HAL_UART_RXEVENT_TC
  uart_handle_->RxEventType = HAL_UART_RXEVENT_TC;
#endif

#if defined(UART_CLEAR_OREF) && defined(UART_CLEAR_NEF) && defined(UART_CLEAR_PEF) && \
    defined(UART_CLEAR_FEF)
  __HAL_UART_CLEAR_FLAG(
      uart_handle_, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF | UART_CLEAR_FEF);
#elif defined(__HAL_UART_CLEAR_PEFLAG)
  __HAL_UART_CLEAR_PEFLAG(uart_handle_);
#endif

#ifdef UART_TXDATA_FLUSH_REQUEST
  __HAL_UART_SEND_REQ(uart_handle_, UART_TXDATA_FLUSH_REQUEST);
#endif
#ifdef UART_RXDATA_FLUSH_REQUEST
  __HAL_UART_SEND_REQ(uart_handle_, UART_RXDATA_FLUSH_REQUEST);
#elif defined(__HAL_UART_FLUSH_DRREGISTER)
  __HAL_UART_FLUSH_DRREGISTER(uart_handle_);
#endif
  return true;
}

UartDmaAbortJoin::Direction STM32UART::DmaDirection(DMA_HandleTypeDef* dma_handle) const
{
  uint32_t direction = 0U;
  if (dma_handle == uart_handle_->hdmatx)
  {
    direction |= UartDmaAbortJoin::Mask(UartDmaAbortJoin::Direction::TX);
  }
  if (dma_handle == uart_handle_->hdmarx)
  {
    direction |= UartDmaAbortJoin::Mask(UartDmaAbortJoin::Direction::RX);
  }
  ASSERT(direction != 0U);
  ASSERT(direction != UartDmaAbortJoin::ALL_DIRECTIONS);
  return static_cast<UartDmaAbortJoin::Direction>(direction);
}

bool STM32UART::DmaIsStopped(DMA_HandleTypeDef* dma_handle) const
{
#if defined(DMA_SxCR_EN) && defined(BDMA_CCR_EN) && defined(IS_DMA_STREAM_INSTANCE)
  if (IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U)
  {
    return ((static_cast<DMA_Stream_TypeDef*>(dma_handle->Instance)->CR & DMA_SxCR_EN) ==
            0U);
  }
  return ((static_cast<BDMA_Channel_TypeDef*>(dma_handle->Instance)->CCR & BDMA_CCR_EN) ==
          0U);
#elif defined(DMA_SxCR_EN)
  return ((dma_handle->Instance->CR & DMA_SxCR_EN) == 0U);
#elif defined(DMA_CCR_EN)
  // STM32 DMA Channel and H7 BDMA expose the enable control in CCR.
  return ((dma_handle->Instance->CCR & DMA_CCR_EN) == 0U);
#else
  UNUSED(dma_handle);
  return false;
#endif
}

void STM32UART::DisableUartInterrupts()
{
#ifdef UART_IT_PE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_PE);
#endif
#ifdef UART_IT_RXNE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RXNE);
#endif
#ifdef UART_IT_RXNE_RXFNE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RXNE_RXFNE);
#endif
#ifdef UART_IT_RXFNE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RXFNE);
#endif
#ifdef UART_IT_TXE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TXE);
#endif
#ifdef UART_IT_TXE_TXFNF
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TXE_TXFNF);
#endif
#ifdef UART_IT_TXFNF
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TXFNF);
#endif
#ifdef UART_IT_TC
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TC);
#endif
#ifdef UART_IT_IDLE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_IDLE);
#endif
#ifdef UART_IT_LBD
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_LBD);
#endif
#ifdef UART_IT_CTS
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_CTS);
#endif
#ifdef UART_IT_CM
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_CM);
#endif
#ifdef UART_IT_WUF
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_WUF);
#endif
#ifdef UART_IT_RXFF
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RXFF);
#endif
#ifdef UART_IT_TXFE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TXFE);
#endif
#ifdef UART_IT_RXFT
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RXFT);
#endif
#ifdef UART_IT_TXFT
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_TXFT);
#endif
#ifdef UART_IT_RTO
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_RTO);
#endif
#ifdef UART_IT_EIE
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_EIE);
#endif
#ifdef UART_IT_ERR
  __HAL_UART_DISABLE_IT(uart_handle_, UART_IT_ERR);
#endif
}

void STM32UART::DisableDmaInterrupts(DMA_HandleTypeDef* dma_handle)
{
#if defined(__HAL_DMA_DISABLE_IT)
  uint32_t interrupt_mask = 0U;
#ifdef DMA_IT_TC
  interrupt_mask |= DMA_IT_TC;
#endif
#ifdef DMA_IT_HT
  interrupt_mask |= DMA_IT_HT;
#endif
#ifdef DMA_IT_TE
  interrupt_mask |= DMA_IT_TE;
#endif
#ifdef DMA_IT_DME
  interrupt_mask |= DMA_IT_DME;
#endif
  if (interrupt_mask != 0U)
  {
    __HAL_DMA_DISABLE_IT(dma_handle, interrupt_mask);
  }
#ifdef DMA_IT_FE
  // Stream HALs select FCR only when the argument is exactly DMA_IT_FE.
  __HAL_DMA_DISABLE_IT(dma_handle, DMA_IT_FE);
#endif
#else
  UNUSED(dma_handle);
#endif
  STM32_DMA_DisableDmamuxInterrupts(dma_handle);
}

void STM32UART::EnableDmaAbortInterrupt(DMA_HandleTypeDef* dma_handle)
{
#if defined(__HAL_DMA_ENABLE_IT) && defined(DMA_SxCR_EN)
#if defined(IS_DMA_STREAM_INSTANCE)
  if (IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U)
  {
    uint32_t interrupt_mask = 0U;
#ifdef DMA_IT_TC
    interrupt_mask |= DMA_IT_TC;
#endif
#ifdef DMA_IT_TE
    interrupt_mask |= DMA_IT_TE;
#endif
#ifdef DMA_IT_DME
    interrupt_mask |= DMA_IT_DME;
#endif
    if (interrupt_mask != 0U)
    {
      __HAL_DMA_ENABLE_IT(dma_handle, interrupt_mask);
    }
#ifdef DMA_IT_FE
    // Stream HALs select FCR only when the argument is exactly DMA_IT_FE.
    __HAL_DMA_ENABLE_IT(dma_handle, DMA_IT_FE);
#endif
  }
#else
  uint32_t interrupt_mask = 0U;
#ifdef DMA_IT_TC
  interrupt_mask |= DMA_IT_TC;
#endif
#ifdef DMA_IT_TE
  interrupt_mask |= DMA_IT_TE;
#endif
#ifdef DMA_IT_DME
  interrupt_mask |= DMA_IT_DME;
#endif
  if (interrupt_mask != 0U)
  {
    __HAL_DMA_ENABLE_IT(dma_handle, interrupt_mask);
  }
#ifdef DMA_IT_FE
  __HAL_DMA_ENABLE_IT(dma_handle, DMA_IT_FE);
#endif
#endif
#else
  UNUSED(dma_handle);
#endif
}

void STM32UART::ClearDmaFlags(DMA_HandleTypeDef* dma_handle)
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
  if (flags != 0U)
  {
    __HAL_DMA_CLEAR_FLAG(dma_handle, flags);
  }
  STM32_DMA_ClearDmamuxFlags(dma_handle);
#else
  UNUSED(dma_handle);
#endif
}

void STM32UART::ReadBackDmaControl(DMA_HandleTypeDef* dma_handle)
{
#if defined(DMA_SxCR_EN) && defined(BDMA_CCR_EN) && defined(IS_DMA_STREAM_INSTANCE)
  if (IS_DMA_STREAM_INSTANCE(dma_handle->Instance) != 0U)
  {
    const volatile uint32_t control =
        static_cast<DMA_Stream_TypeDef*>(dma_handle->Instance)->CR;
    UNUSED(control);
  }
  else
  {
    const volatile uint32_t control =
        static_cast<BDMA_Channel_TypeDef*>(dma_handle->Instance)->CCR;
    UNUSED(control);
  }
#elif defined(DMA_SxCR_EN)
  const volatile uint32_t control = dma_handle->Instance->CR;
  UNUSED(control);
#elif defined(DMA_CCR_EN)
  // STM32 DMA Channel and H7 BDMA expose the enable control in CCR.
  const volatile uint32_t control = dma_handle->Instance->CCR;
  UNUSED(control);
#else
  UNUSED(dma_handle);
#endif

#if defined(DMAMUX_CxCR_SOIE)
  if (STM32_DMA_HasDmamux(dma_handle) && (dma_handle->DMAmuxChannel != nullptr))
  {
    const volatile uint32_t dmamux_control = dma_handle->DMAmuxChannel->CCR;
    UNUSED(dmamux_control);
#if defined(DMAMUX_RGxCR_OIE)
    if (dma_handle->DMAmuxRequestGen != nullptr)
    {
      const volatile uint32_t request_control = dma_handle->DMAmuxRequestGen->RGCR;
      UNUSED(request_control);
    }
#endif
  }
#endif
}

void STM32UART::SynchronizeDisabledInterrupts()
{
  const volatile uint32_t uart_control = uart_handle_->Instance->CR1;
  UNUSED(uart_control);
  if (uart_handle_->hdmatx != nullptr)
  {
    ReadBackDmaControl(uart_handle_->hdmatx);
  }
  if (uart_handle_->hdmarx != nullptr)
  {
    ReadBackDmaControl(uart_handle_->hdmarx);
  }
  __DSB();
}

void STM32UART::FinalizeStoppedDma(DMA_HandleTypeDef* dma_handle, bool in_isr)
{
  if (!DmaIsStopped(dma_handle))
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return;
  }

  DisableDmaInterrupts(dma_handle);
  ReadBackDmaControl(dma_handle);
  __DSB();
  ClearDmaFlags(dma_handle);
  __DSB();

  // These fields are common to the supported Stream/Channel/BDMA HAL handles. No HAL
  // callback is allowed across this normalization boundary.
  dma_handle->XferAbortCallback = nullptr;
  dma_handle->ErrorCode = HAL_DMA_ERROR_NONE;
  dma_handle->State = HAL_DMA_STATE_READY;
  dma_handle->Lock = HAL_UNLOCKED;
}

bool STM32UART::LaunchDmaAbort(DMA_HandleTypeDef* dma_handle,
                               UartDmaAbortJoin::Direction direction, bool in_isr)
{
  dma_handle->XferAbortCallback = nullptr;
  if (DmaIsStopped(dma_handle))
  {
    (void)abort_join_.CompleteStopped(direction);
    return false;
  }

  const HAL_StatusTypeDef result = HAL_DMA_Abort_IT(dma_handle);
  if (DmaIsStopped(dma_handle))
  {
    (void)abort_join_.CompleteStopped(direction);
    return false;
  }

  const bool accepted = result == HAL_OK;
  bool already_aborting = false;
#if defined(DMA_SxCR_EN)
  already_aborting =
      STM32_DMA_IsAsyncStream(dma_handle) && (dma_handle->State == HAL_DMA_STATE_ABORT);
#endif
  if (!accepted && !already_aborting)
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return false;
  }
  if (!STM32_DMA_IsAsyncStream(dma_handle))
  {
    // Channel and BDMA abort paths are synchronous. Returning with EN still set is
    // outside the supported family contract and must not be treated as asynchronous.
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return false;
  }

  // TC/TE/DME/FE may predate this abort, especially for circular RX. Clear them only
  // after requesting EN-clear, then re-read EN. A later stop produces a fresh terminal
  // flag that wakes the armed wrapper.
  DisableDmaInterrupts(dma_handle);
  ClearDmaFlags(dma_handle);
  ReadBackDmaControl(dma_handle);
  __DSB();
  if (DmaIsStopped(dma_handle))
  {
    (void)abort_join_.CompleteStopped(direction);
    return false;
  }
  return true;
}

bool STM32UART::ArmAsyncStop(DMA_HandleTypeDef* dma_handle,
                             UartDmaAbortJoin::Direction direction, bool in_isr)
{
  DEV_ASSERT_FROM_CALLBACK(STM32_DMA_IsAsyncStream(dma_handle), in_isr);
  DisableDmaInterrupts(dma_handle);
  ClearDmaFlags(dma_handle);
  ReadBackDmaControl(dma_handle);
  __DSB();
  if (DmaIsStopped(dma_handle))
  {
    (void)abort_join_.CompleteStopped(direction);
    return false;
  }

  // Complete all peripheral RMW before publishing ASYNC_STOP_ARMED. A stale
  // NVIC-pending wrapper that runs before publication takes the normal deferred path; a
  // wrapper that observes publication cannot overlap these register writes.
  EnableDmaAbortInterrupt(dma_handle);
  ReadBackDmaControl(dma_handle);
  __DSB();
  const bool armed = abort_join_.ArmAsyncStop(direction);
  DEV_ASSERT_FROM_CALLBACK(armed, in_isr);
  return armed;
}

bool STM32UART::AnyAsyncStopArmed() const
{
  using Direction = UartDmaAbortJoin::Direction;
  return abort_join_.AsyncStopArmed(Direction::TX) ||
         abort_join_.AsyncStopArmed(Direction::RX);
}

void STM32UART::DmaIRQHandler(DMA_HandleTypeDef* dma_handle)
{
  ASSERT(dma_handle != nullptr);
  ASSERT(dma_handle->Parent == uart_handle_);
  const bool in_isr = STM32_UART_InIsr();

  const UartDmaAbortJoin::Direction direction = DmaDirection(dma_handle);
  if (abort_join_.AsyncStopArmed(direction))
  {
    // An armed Stream IRQ is only a wakeup for an EN recheck. Passing stale circular-RX
    // TC/error flags to HAL could produce an abort callback while EN is still set.
    DisableDmaInterrupts(dma_handle);
    ClearDmaFlags(dma_handle);
    ReadBackDmaControl(dma_handle);
    __DSB();
    const bool stopped = DmaIsStopped(dma_handle);
    if (!stopped)
    {
      EnableDmaAbortInterrupt(dma_handle);
      ReadBackDmaControl(dma_handle);
      __DSB();
    }

    if (abort_join_.FinishAsyncStopIrq(direction, stopped))
    {
      tx_dma_model_.ResumeConfig(in_isr);
    }
    return;
  }

  UartHardwareGate::OwnerContext hardware_context;
  if (!hardware_gate_.TryEnterIrq(hardware_context))
  {
    DeferNormalIrq();
    return;
  }

  DispatchNormalDmaIrq(dma_handle, hardware_context);
  DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
}

void STM32UART::UartIRQHandler()
{
  const bool in_isr = STM32_UART_InIsr();
  UartHardwareGate::OwnerContext hardware_context;
  if (!hardware_gate_.TryEnterIrq(hardware_context))
  {
    DeferNormalIrq();
    return;
  }

  DispatchNormalUartIrq(hardware_context);
  DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), in_isr);
}

void STM32UART::PublishIrqDomainRequest(IrqDomainRequest request)
{
  const uint32_t request_bits = static_cast<uint32_t>(request);
  ASSERT(request_bits != 0U);
  ASSERT((request_bits & ~IRQ_DOMAIN_REQUEST_MASK) == 0U);

  // The request and SCHEDULED marker are one publication. Only the transition from
  // idle to scheduled owns a kick; publishers arriving while the handler is draining
  // leave their level-triggered request for its next release/check iteration.
  const uint32_t previous = irq_domain_state_.fetch_or(
      request_bits | IRQ_DOMAIN_SCHEDULED, std::memory_order_release);
  if ((previous & IRQ_DOMAIN_SCHEDULED) == 0U)
  {
    irq_domain_ops_.kick_target(irq_domain_ops_.context);
  }
}

void STM32UART::DeferNormalIrq()
{
  irq_domain_ops_.mask_normal(irq_domain_ops_.context);
  hardware_gate_.MarkIrqDeferred();
  PublishIrqDomainRequest(IrqDomainRequest::DEFERRED_SCAN);

  // CONFIG may arm a Stream stop after this wrapper's first admission load. A target
  // handler may also consume an earlier RESTORE before this mask completes. Rechecking
  // after the mask guarantees either this publisher or CONFIG publishes a fresh restore.
  if (AnyAsyncStopArmed())
  {
    PublishIrqDomainRequest(IrqDomainRequest::RESTORE_NORMAL);
  }
}

void STM32UART::DispatchNormalDmaIrq(DMA_HandleTypeDef* dma_handle,
                                     UartHardwareGate::OwnerContext& context)
{
  CallbackScope scope(*this, &context, CallbackDispatch::NORMAL, dma_handle);
  HAL_DMA_IRQHandler(dma_handle);
}

void STM32UART::DispatchNormalUartIrq(UartHardwareGate::OwnerContext& context)
{
  CallbackScope scope(*this, &context, CallbackDispatch::NORMAL, nullptr);
  HAL_UART_IRQHandler(uart_handle_);
}

void STM32UART::ScanDeferredIrqDomain(UartHardwareGate::OwnerContext& context)
{
  // DMA error/complete status is consumed before UART TC so an old TX DMA terminal
  // result cannot be mistaken for completion of a transfer started by that scan.
  if (uart_handle_->hdmarx != nullptr)
  {
    DispatchNormalDmaIrq(uart_handle_->hdmarx, context);
  }
  if (uart_handle_->hdmatx != nullptr)
  {
    DispatchNormalDmaIrq(uart_handle_->hdmatx, context);
  }
  DispatchNormalUartIrq(context);
}

void STM32UART::IrqDomainHandler()
{
  const uint32_t deferred_scan = static_cast<uint32_t>(IrqDomainRequest::DEFERRED_SCAN);
  const uint32_t restore_normal = static_cast<uint32_t>(IrqDomainRequest::RESTORE_NORMAL);

  // The BSP contract guarantees that kicks invoke this routine later and do not overlap
  // it recursively. SCHEDULED remains set for the whole drain, so a duplicate stale
  // kick observes no independent work and cannot steal a publisher's snapshot.
  uint32_t snapshot =
      irq_domain_state_.exchange(IRQ_DOMAIN_SCHEDULED, std::memory_order_acq_rel);
  for (;;)
  {
    const uint32_t requests = snapshot & IRQ_DOMAIN_REQUEST_MASK;
    bool restored_normal = false;

    if ((requests & deferred_scan) != 0U)
    {
      UartHardwareGate::OwnerContext hardware_context;
      if (hardware_gate_.TryEnterDeferredIrq(hardware_context))
      {
        irq_domain_ops_.mask_normal(irq_domain_ops_.context);
        ScanDeferredIrqDomain(hardware_context);
        irq_domain_ops_.restore_normal(irq_domain_ops_.context);
        restored_normal = true;
        DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context),
                                STM32_UART_InIsr());
      }
      // A failed deferred admission intentionally consumes only this notification. The
      // gate's IRQ_DEFERRED_PENDING bit is durable; the owner that currently blocks the
      // claim (CONFIG, IRQ, or TX start) republishes a fresh scan obligation when it
      // leaves. This prevents a CONFIG-held handler from spinning on a level request.
    }

    // RESTORE_NORMAL is an NVIC-domain handoff, not a hardware-state scan. It is safe
    // while CONFIG owns the gate because CONFIG has disabled all ordinary UART/DMA
    // peripheral sources and has admitted only the selected asynchronous-abort source.
    // If a successful deferred scan already restored the authoritative mask, one call
    // is sufficient for both request bits.
    if (((requests & restore_normal) != 0U) && !restored_normal)
    {
      irq_domain_ops_.restore_normal(irq_domain_ops_.context);
    }

    // Release SCHEDULED only after the snapshot has been handled. If a publisher set a
    // request before this CAS, the CAS fails and the next exchange takes that request.
    // If it races after the CAS, it observes the idle word and owns a fresh kick; the
    // CAS is therefore the release/check boundary and no extra load is needed.
    uint32_t expected = IRQ_DOMAIN_SCHEDULED;
    if (!irq_domain_state_.compare_exchange_strong(
            expected, 0U, std::memory_order_release, std::memory_order_acquire))
    {
      snapshot =
          irq_domain_state_.exchange(IRQ_DOMAIN_SCHEDULED, std::memory_order_acq_rel);
      continue;
    }

    return;
  }
}

void STM32UART::SetRxDMA()
{
  if ((uart_handle_->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
  {
    ASSERT(uart_handle_->hdmarx != NULL);

    rx_dma_model_.Start(*this);
    _read_port = ReadFun;
  }
}

void STM32UART::OnRxDataAvailable(bool in_isr)
{
  if (rx_admission_.load(std::memory_order_acquire) == 0U)
  {
    return;
  }

  rx_dma_model_.OnDataAvailable(*this, _read_port, in_isr);
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  if ((uart->callback_dispatch_ != STM32UART::CallbackDispatch::NORMAL) ||
      (uart->callback_context_ == nullptr))
  {
    DEV_ASSERT_FROM_CALLBACK(false, STM32_UART_InIsr());
    return;
  }

  uart->OnRxDataAvailable(STM32_UART_InIsr());
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  if ((uart->callback_dispatch_ != STM32UART::CallbackDispatch::NORMAL) ||
      (uart->callback_context_ == nullptr))
  {
    DEV_ASSERT_FROM_CALLBACK(false, STM32_UART_InIsr());
    return;
  }

  uart->tx_dma_model_.OnTransferDone(STM32_UART_InIsr(), *uart->callback_context_);
}

extern "C" __attribute__((used)) void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
  auto* uart = STM32UART::map[stm32_uart_get_id(huart->Instance)];
  ASSERT(uart != nullptr);
  const bool in_isr = STM32_UART_InIsr();
  if ((uart->callback_dispatch_ != STM32UART::CallbackDispatch::NORMAL) ||
      (uart->callback_context_ == nullptr))
  {
    DEV_ASSERT_FROM_CALLBACK(false, in_isr);
    return;
  }

  if ((uart->callback_dma_ != nullptr) && (uart->callback_dma_ == huart->hdmatx))
  {
    uart->tx_dma_model_.OnTransferError(in_isr, *uart->callback_context_);
  }
  else
  {
    // UART line errors and RX-DMA errors do not prove that the TX DMA terminal state is
    // invalid. Re-enter the destructive CONFIG path so RX and UART state are recovered
    // without falsely completing or releasing the TX active record.
    uart->tx_dma_model_.RequestConfig(in_isr);
  }
}

UartDmaTxStartResult STM32UART::StartDmaTx(
    uint8_t* data, size_t size, int, UartHardwareGate::OwnerContext* hardware_context)
{
  const bool nested_start = hardware_context != nullptr;
  if (nested_start && !hardware_gate_.TryEnterNestedTxStart(*hardware_context))
  {
    return UartDmaTxStartResult::RETRY;
  }
  if (!nested_start && !hardware_gate_.TryEnterTxStart())
  {
    return UartDmaTxStartResult::RETRY;
  }

  const auto finish_start = [&](UartDmaTxStartResult result)
  {
    if (nested_start)
    {
      hardware_gate_.LeaveNestedTxStart(*hardware_context);
    }
    else
    {
      DispatchHardwareActions(hardware_gate_.LeaveTxStart(), STM32_UART_InIsr());
    }
    return result;
  };

  STM32_CleanDCacheByAddr(data, size);
  if (HAL_UART_Transmit_DMA(uart_handle_, data, size) != HAL_OK)
  {
    return finish_start(UartDmaTxStartResult::FAILED);
  }
  return finish_start(UartDmaTxStartResult::STARTED);
}

void STM32UART::DispatchHardwareActions(UartHardwareGate::PendingAction actions,
                                        bool in_isr)
{
  if (UartHardwareGate::HasAction(actions, UartHardwareGate::PendingAction::IRQ_DEFERRED))
  {
    // IRQ_DEFERRED is a target-core rescan obligation. Releasing an owner may only
    // publish and kick it; arbitrary-core callers must not scan or restore the NVIC
    // domain directly.
    PublishIrqDomainRequest(IrqDomainRequest::DEFERRED_SCAN);
  }

  if (UartHardwareGate::HasAction(actions, UartHardwareGate::PendingAction::CONFIG))
  {
    tx_dma_model_.ResumeConfig(in_isr);
    return;
  }

  if (UartHardwareGate::HasAction(actions, UartHardwareGate::PendingAction::TX_START))
  {
    tx_dma_model_.ResumeStart(in_isr);
  }
}

void LibXR::STM32_UART_DMA_IRQHandler(DMA_HandleTypeDef* dma_handle)
{
  ASSERT(dma_handle != nullptr);
  auto* uart_handle = static_cast<UART_HandleTypeDef*>(dma_handle->Parent);
  ASSERT(uart_handle != nullptr);
  auto* uart = STM32UART::map[stm32_uart_get_id(uart_handle->Instance)];
  ASSERT(uart != nullptr);
  uart->DmaIRQHandler(dma_handle);
}

void LibXR::STM32_UART_IRQHandler(UART_HandleTypeDef* uart_handle)
{
  ASSERT(uart_handle != nullptr);
  auto* uart = STM32UART::map[stm32_uart_get_id(uart_handle->Instance)];
  ASSERT(uart != nullptr);
  uart->UartIRQHandler();
}

#endif
