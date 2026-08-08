#include "ch32_uart.hpp"

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

namespace
{

constexpr uint32_t CH32_DMA_CONTROLLER_SELECTOR_MASK = 0x30000000U;
constexpr size_t CH32_DMA_MAX_TRANSFER_SIZE = UINT16_MAX;

bool Ch32BaudrateRepresentable(uint32_t peripheral_clock, uint32_t baudrate)
{
  if (baudrate == 0U)
  {
    return false;
  }

  const uint64_t integer_divider = (25ULL * peripheral_clock) / (4ULL * baudrate);
  uint64_t brr = (integer_divider / 100ULL) << 4U;
  const uint64_t fractional_divider = integer_divider - (100ULL * (brr >> 4U));
  brr |= (((fractional_divider * 16ULL) + 50ULL) / 100ULL) & 0x0FULL;
  return (brr >= 0x10ULL) && (brr <= UINT16_MAX);
}

bool FillCh32UsartConfig(UART::Configuration config, uint16_t mode,
                         USART_InitTypeDef& usart_cfg, bool in_isr)
{
  usart_cfg = {};
  usart_cfg.USART_BaudRate = config.baudrate;
  usart_cfg.USART_StopBits =
      (config.stop_bits == 2U) ? USART_StopBits_2 : USART_StopBits_1;
  usart_cfg.USART_Mode = mode;
  usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;

  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      usart_cfg.USART_Parity = USART_Parity_No;
      usart_cfg.USART_WordLength = USART_WordLength_8b;
      return true;
    case UART::Parity::EVEN:
      usart_cfg.USART_Parity = USART_Parity_Even;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      return true;
    case UART::Parity::ODD:
      usart_cfg.USART_Parity = USART_Parity_Odd;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      return true;
    default:
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return false;
  }
}

constexpr uint32_t Ch32DmaControllerSelector(uint32_t complete_status)
{
  const uint32_t selector = complete_status & CH32_DMA_CONTROLLER_SELECTOR_MASK;
  return ((complete_status & ~selector) != 0U) ? selector : 0U;
}

constexpr uint32_t Ch32DmaTransferErrorStatus(uint32_t complete_status)
{
  const uint32_t selector = Ch32DmaControllerSelector(complete_status);
  return selector | ((complete_status & ~selector) << 2U);
}

constexpr uint32_t Ch32DmaGlobalStatus(uint32_t complete_status)
{
  const uint32_t selector = Ch32DmaControllerSelector(complete_status);
  return selector | ((complete_status & ~selector) >> 1U);
}

static_assert(Ch32DmaTransferErrorStatus(DMA1_IT_TC1) == DMA1_IT_TE1);
static_assert(Ch32DmaGlobalStatus(DMA1_IT_TC1) == DMA1_IT_GL1);
static_assert(Ch32DmaTransferErrorStatus(DMA1_IT_TC7) == DMA1_IT_TE7);
static_assert(Ch32DmaGlobalStatus(DMA1_IT_TC7) == DMA1_IT_GL7);
static_assert(Ch32DmaTransferErrorStatus(0x20000000U) == 0x80000000U);
static_assert(Ch32DmaGlobalStatus(0x20000000U) == 0x10000000U);
#if defined(DMA1_IT_TC8)
static_assert(Ch32DmaTransferErrorStatus(DMA1_IT_TC8) == DMA1_IT_TE8);
static_assert(Ch32DmaGlobalStatus(DMA1_IT_TC8) == DMA1_IT_GL8);
#endif
#if defined(DMA2_IT_TC1)
static_assert(Ch32DmaTransferErrorStatus(DMA2_IT_TC1) == DMA2_IT_TE1);
static_assert(Ch32DmaGlobalStatus(DMA2_IT_TC1) == DMA2_IT_GL1);
#endif
#if defined(DMA2_IT_TC8)
static_assert(Ch32DmaTransferErrorStatus(DMA2_IT_TC8) == DMA2_IT_TE8);
static_assert(Ch32DmaGlobalStatus(DMA2_IT_TC8) == DMA2_IT_GL8);
#endif

void Ch32UartIoFence() { __asm__ volatile("fence iorw, iorw" ::: "memory"); }

void Ch32ClearDmaStatus(uint32_t status)
{
  DMA_ClearITPendingBit(status);
  Ch32UartIoFence();
  (void)DMA_GetITStatus(status);
}

void Ch32MaskDmaTerminalInterrupts(DMA_Channel_TypeDef* channel, bool in_isr)
{
  DMA_ITConfig(channel, DMA_IT_TC | DMA_IT_TE, DISABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((channel->CFGR & (DMA_IT_TC | DMA_IT_TE)) == 0U, in_isr);
}

void Ch32StopAndClearDmaChannel(DMA_Channel_TypeDef* channel, uint32_t channel_status,
                                bool in_isr)
{
  DMA_Cmd(channel, DISABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((channel->CFGR & DMA_CFGR1_EN) == 0U, in_isr);
  Ch32ClearDmaStatus(channel_status);
  DEV_ASSERT_FROM_CALLBACK(DMA_GetITStatus(channel_status) == RESET, in_isr);
}

UartOldTxTerminal Ch32StopAndClassifyTxDmaChannel(DMA_Channel_TypeDef* channel,
                                                  uint32_t complete_status, bool in_isr)
{
  DMA_Cmd(channel, DISABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((channel->CFGR & DMA_CFGR1_EN) == 0U, in_isr);

  const uint32_t error_status = Ch32DmaTransferErrorStatus(complete_status);
  const bool complete = DMA_GetITStatus(complete_status) == SET;
  const bool error = DMA_GetITStatus(error_status) == SET;

  const uint32_t global_status = Ch32DmaGlobalStatus(complete_status);
  Ch32ClearDmaStatus(global_status);
  DEV_ASSERT_FROM_CALLBACK(DMA_GetITStatus(global_status) == RESET, in_isr);

  if (error)
  {
    return UartOldTxTerminal::ERROR;
  }
  return complete ? UartOldTxTerminal::COMPLETE : UartOldTxTerminal::NONE;
}

ch32_uart_id_t CheckedUartId(ch32_uart_id_t id)
{
  REQUIRE(static_cast<size_t>(id) < static_cast<size_t>(CH32_UART_NUMBER));
  return id;
}

}  // namespace

CH32UART* CH32UART::map_[ch32_uart_id_t::CH32_UART_NUMBER] = {nullptr};

bool CH32UART::InIsr()
{
  constexpr size_t ACTIVE_WORD_COUNT = sizeof(PFIC->IACTR) / sizeof(PFIC->IACTR[0]);
  for (size_t index = 0U; index < ACTIVE_WORD_COUNT; ++index)
  {
    if (PFIC->IACTR[index] != 0U)
    {
      return true;
    }
  }
  return false;
}

CH32UART::CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx,
                   GPIO_TypeDef* tx_gpio_port, uint16_t tx_gpio_pin,
                   GPIO_TypeDef* rx_gpio_port, uint16_t rx_gpio_pin, uint32_t pin_remap,
                   uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      id_(CheckedUartId(id)),
      _read_port(dma_rx.size_),
      _write_port(tx_queue_size, dma_tx.size_ / 2),
      rx_dma_model_(dma_rx),
      dma_model_(*this, execution_policy_, _write_port, dma_tx),
      instance_(ch32_uart_get_instance_id(id_)),
      dma_rx_channel_(CH32_UART_RX_DMA_CHANNEL_MAP[id_]),
      dma_tx_channel_(CH32_UART_TX_DMA_CHANNEL_MAP[id_])
{
  REQUIRE(map_[id_] == nullptr);
  map_[id_] = this;

  bool tx_enable = dma_tx.size_ > 1;
  bool rx_enable = dma_rx.size_ > 0;

  REQUIRE(tx_enable || rx_enable);
  REQUIRE((dma_tx.size_ == 0U) ||
          ((dma_tx.addr_ != nullptr) &&
           ((reinterpret_cast<uintptr_t>(dma_tx.addr_) % alignof(size_t)) == 0U) &&
           ((dma_tx.size_ % (2U * alignof(size_t))) == 0U) &&
           ((dma_tx.size_ / 2U) <= CH32_DMA_MAX_TRANSFER_SIZE)));
  REQUIRE(!rx_enable || (dma_rx.size_ <= CH32_DMA_MAX_TRANSFER_SIZE));
  REQUIRE(ValidateConfig(config) == ErrorCode::OK);
  if (tx_enable)
  {
    ASSERT(dma_tx_channel_ != nullptr);
    ASSERT(CH32_UART_TX_DMA_IT_MAP[id_] != 0);
    ASSERT(Ch32DmaTransferErrorStatus(CH32_UART_TX_DMA_IT_MAP[id_]) != 0U);
    ASSERT(Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]) != 0U);
  }
  if (rx_enable)
  {
    ASSERT(dma_rx_channel_ != nullptr);
    ASSERT(CH32_UART_RX_DMA_IT_TC_MAP[id_] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_HT_MAP[id_] != 0);
    ASSERT(Ch32DmaTransferErrorStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]) != 0U);
    ASSERT(Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]) != 0U);
  }

  GPIO_InitTypeDef gpio_init = {};
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

  if (tx_enable)
  {
    RCC_APB2PeriphClockCmd(ch32_get_gpio_periph(tx_gpio_port), ENABLE);
    gpio_init.GPIO_Pin = tx_gpio_pin;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(tx_gpio_port, &gpio_init);
    (*write_port_) = WriteFun;
  }

  if (rx_enable)
  {
    RCC_APB2PeriphClockCmd(ch32_get_gpio_periph(rx_gpio_port), ENABLE);
    gpio_init.GPIO_Pin = rx_gpio_pin;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(rx_gpio_port, &gpio_init);
  }

  if (pin_remap != 0)
  {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(pin_remap, ENABLE);
  }

  if (CH32_UART_APB_MAP[id_] == 1)
  {
    RCC_APB1PeriphClockCmd(CH32_UART_RCC_PERIPH_MAP[id_], ENABLE);
  }
  else if (CH32_UART_APB_MAP[id_] == 2)
  {
    RCC_APB2PeriphClockCmd(CH32_UART_RCC_PERIPH_MAP[id_], ENABLE);
  }
  else
  {
    ASSERT(false);
  }
  RCC_AHBPeriphClockCmd(CH32_UART_RCC_PERIPH_MAP_DMA[id_], ENABLE);

  uart_mode_ = (tx_enable ? USART_Mode_Tx : 0) | (rx_enable ? USART_Mode_Rx : 0);
  USART_InitTypeDef usart_cfg = {};
  REQUIRE(FillCh32UsartConfig(config, uart_mode_, usart_cfg, false));
  USART_Init(instance_, &usart_cfg);

  DMA_InitTypeDef dma_init = {};
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;

  if (rx_enable)
  {
    ch32_dma_callback_t rx_cb_fun = [](void* arg)
    { reinterpret_cast<CH32UART*>(arg)->RxDmaIRQHandler(); };

    ch32_dma_register_callback(ch32_dma_get_id(CH32_UART_RX_DMA_CHANNEL_MAP[id_]),
                               rx_cb_fun, this);

    DMA_DeInit(dma_rx_channel_);
    dma_init.DMA_PeripheralBaseAddr = reinterpret_cast<uint32_t>(&instance_->DATAR);
    dma_init.DMA_MemoryBaseAddr = reinterpret_cast<uint32_t>(rx_dma_model_.Buffer());
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_Mode = DMA_Mode_Circular;
    dma_init.DMA_BufferSize = rx_dma_model_.BufferSize();
    DMA_Init(dma_rx_channel_, &dma_init);
    DMA_ITConfig(dma_rx_channel_, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE, ENABLE);
    rx_dma_model_.Start(*this);
    USART_DMACmd(instance_, USART_DMAReq_Rx, ENABLE);
  }

  if (tx_enable)
  {
    ch32_dma_callback_t tx_cb_fun = [](void* arg)
    { reinterpret_cast<CH32UART*>(arg)->TxDmaIRQHandler(); };

    ch32_dma_register_callback(ch32_dma_get_id(CH32_UART_TX_DMA_CHANNEL_MAP[id_]),
                               tx_cb_fun, this);
    DMA_DeInit(dma_tx_channel_);
    dma_init.DMA_PeripheralBaseAddr = reinterpret_cast<uint32_t>(&instance_->DATAR);
    dma_init.DMA_MemoryBaseAddr = 0;
    dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_BufferSize = 0;
    DMA_Init(dma_tx_channel_, &dma_init);
    DMA_ITConfig(dma_tx_channel_, DMA_IT_TC | DMA_IT_TE, ENABLE);
    USART_DMACmd(instance_, USART_DMAReq_Tx, ENABLE);
  }

  USART_Cmd(instance_, ENABLE);

  if (rx_enable)
  {
    USART_ITConfig(instance_, USART_IT_IDLE, ENABLE);
    USART_ITConfig(instance_, USART_IT_ERR, ENABLE);
    USART_ITConfig(instance_, USART_IT_PE, ENABLE);
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_rx_channel_)]);
  }

  if (tx_enable)
  {
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_tx_channel_)]);
  }

  NVIC_EnableIRQ(CH32_UART_IRQ_MAP[id_]);
}

ErrorCode CH32UART::SetConfig(UART::Configuration config)
{
  return dma_model_.SetConfig(config, InIsr());
}

ErrorCode CH32UART::ValidateConfig(UART::Configuration config) const
{
  if ((config.baudrate == 0U) || (config.data_bits != 8U) ||
      ((config.stop_bits != 1U) && (config.stop_bits != 2U)))
  {
    return ErrorCode::ARG_ERR;
  }
  if ((config.parity != UART::Parity::NO_PARITY) &&
      (config.parity != UART::Parity::EVEN) && (config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }
  RCC_ClocksTypeDef clocks{};
  RCC_GetClocksFreq(&clocks);
  const uint32_t peripheral_clock =
      (CH32_UART_APB_MAP[id_] == 2U) ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency;
  if (!Ch32BaudrateRepresentable(peripheral_clock, config.baudrate))
  {
    return ErrorCode::ARG_ERR;
  }
  return ErrorCode::OK;
}

UartDmaControlResult CH32UART::AdvanceConfig(UART::Configuration config, bool active_tx,
                                             bool in_isr)
{
  UartOldTxTerminal terminal = UartOldTxTerminal::NONE;
  if (!config_waiting_for_tx_idle_)
  {
    terminal = StopDataPath(active_tx, in_isr);
    if (((uart_mode_ & USART_Mode_Tx) != 0U) &&
        (USART_GetFlagStatus(instance_, USART_FLAG_TC) == RESET))
    {
      config_tx_terminal_ = terminal;
      config_waiting_for_tx_idle_ = true;
    }
  }
  else
  {
    terminal = config_tx_terminal_;
  }

  if (config_waiting_for_tx_idle_)
  {
    // TCIE is used only as the non-blocking carrier for destructive CONFIG. A TC
    // transition between the flag check and this enable remains latched and triggers it.
    USART_ITConfig(instance_, USART_IT_TC, ENABLE);
    Ch32UartIoFence();
    if (USART_GetFlagStatus(instance_, USART_FLAG_TC) == RESET)
    {
      return UartDmaControlResult::Pending();
    }

    USART_ITConfig(instance_, USART_IT_TC, DISABLE);
    Ch32UartIoFence();
    config_waiting_for_tx_idle_ = false;
    config_tx_terminal_ = UartOldTxTerminal::NONE;
  }

  ApplyConfigPayload(config, in_isr);
  USART_Cmd(instance_, ENABLE);
  return UartDmaControlResult::Completed(terminal);
}

UartDmaControlProgress CH32UART::CompleteConfig(bool in_isr)
{
  StartDataPath();
  (void)in_isr;
  return UartDmaControlProgress::COMPLETED;
}

void CH32UART::ApplyConfigPayload(UART::Configuration config, bool in_isr)
{
  USART_InitTypeDef usart_cfg = {};
  if (!FillCh32UsartConfig(config, uart_mode_, usart_cfg, in_isr))
  {
    return;
  }
  USART_DeInit(instance_);
  USART_Init(instance_, &usart_cfg);
}

void CH32UART::SetDataPathInterrupts(bool enabled)
{
  const FunctionalState state = enabled ? ENABLE : DISABLE;
  if ((uart_mode_ & USART_Mode_Tx) != 0U)
  {
    DMA_ITConfig(dma_tx_channel_, DMA_IT_TC | DMA_IT_TE, state);
  }
  if ((uart_mode_ & USART_Mode_Rx) != 0U)
  {
    DMA_ITConfig(dma_rx_channel_, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE, state);
    USART_ITConfig(instance_, USART_IT_IDLE, state);
    USART_ITConfig(instance_, USART_IT_ERR, state);
    USART_ITConfig(instance_, USART_IT_PE, state);
  }
}

UartOldTxTerminal CH32UART::StopDataPath(bool active_tx, bool in_isr)
{
  SetDataPathInterrupts(false);

  UartOldTxTerminal terminal = UartOldTxTerminal::NONE;
  if ((uart_mode_ & USART_Mode_Tx) != 0U)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, DISABLE);
    if (active_tx)
    {
      terminal = Ch32StopAndClassifyTxDmaChannel(dma_tx_channel_,
                                                 CH32_UART_TX_DMA_IT_MAP[id_], in_isr);
    }
    else
    {
      Ch32StopAndClearDmaChannel(
          dma_tx_channel_, Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
    }
  }
  if ((uart_mode_ & USART_Mode_Rx) != 0U)
  {
    USART_DMACmd(instance_, USART_DMAReq_Rx, DISABLE);
    Ch32StopAndClearDmaChannel(
        dma_rx_channel_, Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]), in_isr);
    (void)USART_GetFlagStatus(instance_, USART_FLAG_IDLE);
    (void)USART_ReceiveData(instance_);
  }
  return terminal;
}

void CH32UART::StartDataPath()
{
  if ((uart_mode_ & USART_Mode_Rx) != 0U)
  {
    rx_dma_model_.Start(*this);
    USART_DMACmd(instance_, USART_DMAReq_Rx, ENABLE);
  }
  if ((uart_mode_ & USART_Mode_Tx) != 0U)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, ENABLE);
  }

  SetDataPathInterrupts(true);
  Ch32UartIoFence();
}

ErrorCode CH32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &CH32UART::_write_port);
  return uart->dma_model_.Submit(in_isr);
}

void CH32UART::StartCircularDmaRx(uint8_t* data, size_t size)
{
  const bool in_isr = InIsr();
  REQUIRE_FROM_CALLBACK((size > 0U) && (size <= CH32_DMA_MAX_TRANSFER_SIZE), in_isr);
  dma_rx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_rx_channel_->CNTR = size;
  Ch32UartIoFence();
  DMA_Cmd(dma_rx_channel_, ENABLE);
  Ch32UartIoFence();
  (void)dma_rx_channel_->CFGR;
}

UartDmaTxStartResult CH32UART::StartDmaTx(uint8_t* data, size_t size, int, bool in_isr)
{
  REQUIRE_FROM_CALLBACK((size > 0U) && (size <= CH32_DMA_MAX_TRANSFER_SIZE), in_isr);
  Ch32StopAndClearDmaChannel(dma_tx_channel_,
                             Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
  dma_tx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_tx_channel_->CNTR = size;
  // Arm WCH's documented TC-clear sequence. The first DMA write to DATAR clears TC;
  // if CONFIG stops the channel before that write, TC correctly remains line-idle.
  (void)USART_GetFlagStatus(instance_, USART_FLAG_TC);
  Ch32UartIoFence();
  DMA_Cmd(dma_tx_channel_, ENABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((dma_tx_channel_->CFGR & DMA_CFGR1_EN) != 0U, in_isr);
  return UartDmaTxStartResult::STARTED;
}

extern "C" void ch32_uart_isr_handler_idle(ch32_uart_id_t id)
{
  auto uart = CH32UART::map_[id];
  if (!uart)
  {
    return;
  }
  uart->UartIRQHandler();
}

void CH32UART::TxDmaIRQHandler() { HandleNormalIrq(); }

void CH32UART::RxDmaIRQHandler() { HandleNormalIrq(); }

void CH32UART::UartIRQHandler() { HandleNormalIrq(); }

void CH32UART::HandleNormalIrq()
{
  bool pushed_any = false;
  (void)dma_model_.InvokeIrq(
      [this, &pushed_any]() { return ScanNormalIrqStatus(true, pushed_any); }, true);
  Ch32UartIoFence();
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(true);
  }
}

UartDmaControlResult CH32UART::AdvanceRecovery(bool active_tx, bool in_isr)
{
  return UartDmaControlResult::Completed(StopDataPath(active_tx, in_isr));
}

UartDmaControlProgress CH32UART::CompleteRecovery(bool)
{
  StartDataPath();
  return UartDmaControlProgress::COMPLETED;
}

uint32_t CH32UART::ScanNormalIrqStatus(bool in_isr, bool& pushed_any)
{
  using Model = UartDmaModel<CH32UART, UartDirectPolicy>;

  if (USART_GetITStatus(instance_, USART_IT_TC) != RESET)
  {
    // The latched TC flag remains authoritative until AdvanceConfig() resets USART.
    // Disable only its source; the control continuation is retained by the service.
    USART_ITConfig(instance_, USART_IT_TC, DISABLE);
    Ch32UartIoFence();
    return Model::EventMask(UartDmaEvent::CONTROL_READY);
  }

  const bool rx_enabled = (uart_mode_ & USART_Mode_Rx) != 0U;
  const bool tx_enabled = (uart_mode_ & USART_Mode_Tx) != 0U;
  const uint32_t rx_error_status =
      Ch32DmaTransferErrorStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]);
  const uint32_t tx_error_status =
      Ch32DmaTransferErrorStatus(CH32_UART_TX_DMA_IT_MAP[id_]);
  const bool rx_error = rx_enabled && (DMA_GetITStatus(rx_error_status) == SET);
  const bool rx_half =
      rx_enabled && (DMA_GetITStatus(CH32_UART_RX_DMA_IT_HT_MAP[id_]) == SET);
  const bool rx_complete =
      rx_enabled && (DMA_GetITStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]) == SET);
  const bool tx_error = tx_enabled && (DMA_GetITStatus(tx_error_status) == SET);
  const bool tx_complete =
      tx_enabled && (DMA_GetITStatus(CH32_UART_TX_DMA_IT_MAP[id_]) == SET);

  bool uart_error = false;
#ifdef USART_FLAG_ORE
  uart_error = uart_error || (USART_GetFlagStatus(instance_, USART_FLAG_ORE) != RESET);
#endif
#ifdef USART_FLAG_NE
  uart_error = uart_error || (USART_GetFlagStatus(instance_, USART_FLAG_NE) != RESET);
#endif
#ifdef USART_FLAG_FE
  uart_error = uart_error || (USART_GetFlagStatus(instance_, USART_FLAG_FE) != RESET);
#endif
#ifdef USART_FLAG_PE
  uart_error = uart_error || (USART_GetFlagStatus(instance_, USART_FLAG_PE) != RESET);
#endif

  if (rx_error || tx_error || uart_error)
  {
    // Keep the live TX terminal flags latched. StopDataPath() disables the channel,
    // fences that transition, classifies TC/TE at the stable point, and only then
    // clears the shared W1C status group.
    if (tx_enabled)
    {
      Ch32MaskDmaTerminalInterrupts(dma_tx_channel_, in_isr);
    }
    if (rx_enabled)
    {
      Ch32ClearDmaStatus(Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]));
    }
    if (uart_error)
    {
      (void)USART_ReceiveData(instance_);
    }

    // The TX terminal is intentionally classified by StopDataPath() after DMA EN
    // reaches zero. The status sampled while DMA was live is not authoritative.
    return Model::EventMask(UartDmaEvent::ERROR);
  }

  bool rx_data_available = false;
  if (uart_mode_ & USART_Mode_Rx)
  {
    uint32_t rx_status_to_clear = 0U;
    if (rx_half)
    {
      rx_status_to_clear |= CH32_UART_RX_DMA_IT_HT_MAP[id_];
      rx_data_available = true;
    }
    if (rx_complete)
    {
      rx_status_to_clear |= CH32_UART_RX_DMA_IT_TC_MAP[id_];
      rx_data_available = true;
    }
    if (rx_status_to_clear != 0U)
    {
      Ch32ClearDmaStatus(rx_status_to_clear);
    }
    if (USART_GetFlagStatus(instance_, USART_FLAG_IDLE) != RESET)
    {
      (void)USART_ReceiveData(instance_);
      rx_data_available = true;
    }
  }

  uint32_t events = 0U;
  if (rx_data_available)
  {
    (void)dma_model_.ProcessRxInIrqSource(
        events, [this, &pushed_any]()
        { pushed_any = rx_dma_model_.OnDataAvailable(*this, _read_port); });
  }

  if (tx_complete)
  {
    Ch32StopAndClearDmaChannel(dma_tx_channel_,
                               Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
    events |= Model::EventMask(UartDmaEvent::COMPLETE);
  }
  return events;
}

// USART IRQ entry adapters.
#if defined(USART1)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART1_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART1); }
#endif
#if defined(USART2)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART2_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART2); }
#endif
#if defined(USART3)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART3_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART3); }
#endif
#if defined(USART4)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART4_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART4); }
#endif
#if defined(USART5)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART5_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART5); }
#endif
#if defined(USART6)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART6_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART6); }
#endif
#if defined(USART7)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART7_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART7_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART7); }
#endif
#if defined(USART8)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART8_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void USART8_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_USART8); }
#endif
#if defined(UART1)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART1_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART1); }
#endif
#if defined(UART2)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART2_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART2); }
#endif
#if defined(UART3)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART3_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART3); }
#endif
#if defined(UART4)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART4_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART4); }
#endif
#if defined(UART5)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART5_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART5); }
#endif
#if defined(UART6)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART6_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART6); }
#endif
#if defined(UART7)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART7_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART7_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART7); }
#endif
#if defined(UART8)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART8_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART8_IRQHandler(void) { ch32_uart_isr_handler_idle(CH32_UART8); }
#endif
