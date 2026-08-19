#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

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

}  // namespace

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

void STM32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &STM32UART::_write_port);
  uart->dma_model_.Submit(in_isr);
}

STM32UART::STM32UART(UART_HandleTypeDef* uart_handle, RawData dma_buff_rx,
                     RawData dma_buff_tx, uint32_t tx_queue_size)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2U),
      rx_dma_model_(dma_buff_rx),
#if defined(LIBXR_STM32_UART_GPDMA)
      gpdma_adapter_(uart_handle),
#endif
      dma_model_(*this, execution_policy_, _write_port, dma_buff_tx),
      uart_handle_(uart_handle),
      id_(stm32_uart_get_id((uart_handle == nullptr) ? nullptr : uart_handle->Instance))
{
  REQUIRE(uart_handle_ != nullptr);
  REQUIRE(id_ != STM32_UART_ID_ERROR);
  REQUIRE(map[id_] == nullptr);
  REQUIRE((uart_handle_->hdmatx == nullptr) || (uart_handle_->hdmarx == nullptr) ||
          (uart_handle_->hdmatx != uart_handle_->hdmarx));
#if defined(UART_FIFOMODE_ENABLE)
  REQUIRE(uart_handle_->FifoMode == UART_FIFOMODE_DISABLE);
#endif
  map[id_] = this;

  if ((uart_handle_->Init.Mode & UART_MODE_TX) == UART_MODE_TX)
  {
    REQUIRE(uart_handle_->hdmatx != nullptr);
#if !defined(LIBXR_STM32_UART_GPDMA)
    ASSERT(uart_handle_->hdmatx->Init.Mode == DMA_NORMAL);
#else
    ASSERT(uart_handle_->hdmatx->Mode == DMA_NORMAL);
#endif
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
  }
  SetRxDMA();
}

ErrorCode STM32UART::SetConfig(UART::Configuration config)
{
  return dma_model_.SetConfig(config, InIsr());
}

UartDmaControlResult STM32UART::AdvanceConfig(UART::Configuration config, bool active_tx,
                                              bool in_isr)
{
  UartDmaControlResult stop_result = StopDataPath(active_tx, true, in_isr);
  if (!stop_result.IsCompleted())
  {
    return stop_result;
  }

  const bool configured = ApplyConfigPayload(config, in_isr);
  REQUIRE_FROM_CALLBACK(configured, in_isr);
  return stop_result;
}

UartDmaControlProgress STM32UART::CompleteConfig(bool in_isr)
{
  if (SetRxDMAForControl(in_isr) == UartDmaControlProgress::PENDING)
  {
    return UartDmaControlProgress::PENDING;
  }
  FinishControl();
  return UartDmaControlProgress::COMPLETED;
}

UartDmaControlResult STM32UART::AdvanceRecovery(bool active_tx, bool in_isr)
{
  return StopDataPath(active_tx, false, in_isr);
}

UartDmaControlProgress STM32UART::CompleteRecovery(bool in_isr)
{
  if (SetRxDMAForControl(in_isr) == UartDmaControlProgress::PENDING)
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
    CaptureStoppedTx();
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
      // publishes CONTROL_READY.
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
    FinalizeStopped(uart_handle_->hdmatx, in_isr);
  }
  if (uart_handle_->hdmarx != nullptr)
  {
    FinalizeStopped(uart_handle_->hdmarx, in_isr);
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

#if defined(LIBXR_STM32_UART_GPDMA)
STM32UART::RxArmResult STM32UART::StartLinkedListDmaRx(uint8_t* data, size_t size,
                                                       size_t descriptor_count,
                                                       bool in_isr)
{
  const HAL_StatusTypeDef status =
      gpdma_adapter_.StartLinkedListDmaRx(data, size, descriptor_count, in_isr);
  if (status == HAL_OK)
  {
    rx_arm_result_ = RxArmResult::STARTED;
    return rx_arm_result_;
  }

  if ((status == HAL_ERROR) && IsPendingRxLineError(uart_handle_->ErrorCode))
  {
    rx_arm_result_ = RxArmResult::PENDING_LINE_ERROR;
    return rx_arm_result_;
  }

  rx_arm_result_ = RxArmResult::FAILED;
  REQUIRE_FROM_CALLBACK(false, in_isr);
  return rx_arm_result_;
}

uint8_t* STM32UART::GetLinkedListDmaRxProducer() const
{
  return gpdma_adapter_.GetLinkedListDmaRxProducer();
}

void STM32UART::PrepareLinkedListDmaRxForCpu(uint8_t* data, size_t size)
{
  STM32GpdmaUartAdapter::PrepareLinkedListDmaRxForCpu(data, size);
}
#endif

void STM32UART::SetRxDMA(bool in_isr)
{
  const UartDmaControlProgress result = SetRxDMAForControl(in_isr);
  // A pending line error already owns its HAL error/abort completion carrier.
  REQUIRE_FROM_CALLBACK((result == UartDmaControlProgress::COMPLETED) ||
                            (rx_arm_result_ == RxArmResult::PENDING_LINE_ERROR),
                        in_isr);
}

UartDmaControlProgress STM32UART::SetRxDMAForControl(bool in_isr)
{
  if ((uart_handle_->Init.Mode & UART_MODE_RX) == UART_MODE_RX)
  {
    ASSERT(uart_handle_->hdmarx != nullptr);
    rx_dma_model_.Start(*this, in_isr);
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
  auto queue = _read_port.GetReadQueue(in_isr);
  (void)dma_model_.ProcessRx(
      in_isr, [this, &queue]() { rx_dma_model_.OnDataAvailable(*this, queue); });
  queue.Publish();
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

UartDmaTxStartResult STM32UART::StartDmaTx(uint8_t* data, size_t size, int, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(DmaTransferSizeSupported(size), in_isr);
  STM32_CleanDCacheByAddr(data, size);
  if (HAL_UART_Transmit_DMA(uart_handle_, data, static_cast<uint16_t>(size)) != HAL_OK)
  {
    return UartDmaTxStartResult::FAILED;
  }
  return UartDmaTxStartResult::STARTED;
}

#endif
