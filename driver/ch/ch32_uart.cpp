// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_uart.cpp

#include "ch32_uart.hpp"

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

namespace
{

constexpr uint32_t CH32_DMA_CONTROLLER_SELECTOR_MASK = 0x30000000U;

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

void Ch32StopAndClearDmaChannel(DMA_Channel_TypeDef* channel, uint32_t channel_status,
                                bool in_isr)
{
  DMA_Cmd(channel, DISABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((channel->CFGR & DMA_CFGR1_EN) == 0U, in_isr);
  Ch32ClearDmaStatus(channel_status);
  DEV_ASSERT_FROM_CALLBACK(DMA_GetITStatus(channel_status) == RESET, in_isr);
}

}  // namespace

// Static instance map.
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

// Constructor: USART, DMA, and GPIO initialization.
CH32UART::CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx,
                   GPIO_TypeDef* tx_gpio_port, uint16_t tx_gpio_pin,
                   GPIO_TypeDef* rx_gpio_port, uint16_t rx_gpio_pin, uint32_t pin_remap,
                   uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      id_(id),
      _read_port(dma_rx.size_),
      _write_port(tx_queue_size, dma_tx.size_ / 2),
      requested_config_(config),
      rx_dma_model_(dma_rx),
      tx_dma_model_(*this, _write_port, dma_tx),
      instance_(ch32_uart_get_instance_id(id)),
      dma_rx_channel_(CH32_UART_RX_DMA_CHANNEL_MAP[id]),
      dma_tx_channel_(CH32_UART_TX_DMA_CHANNEL_MAP[id])
{
  map_[id] = this;

  bool tx_enable = dma_tx.size_ > 1;
  bool rx_enable = dma_rx.size_ > 0;

  ASSERT(tx_enable || rx_enable);
  if (tx_enable)
  {
    ASSERT(dma_tx_channel_ != nullptr);
    ASSERT(CH32_UART_TX_DMA_IT_MAP[id] != 0);
    ASSERT(Ch32DmaTransferErrorStatus(CH32_UART_TX_DMA_IT_MAP[id]) != 0U);
    ASSERT(Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id]) != 0U);
  }
  if (rx_enable)
  {
    ASSERT(dma_rx_channel_ != nullptr);
    ASSERT(CH32_UART_RX_DMA_IT_TC_MAP[id] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_HT_MAP[id] != 0);
    ASSERT(Ch32DmaTransferErrorStatus(CH32_UART_RX_DMA_IT_TC_MAP[id]) != 0U);
    ASSERT(Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id]) != 0U);
  }

  /* GPIO配置（TX: 推挽输出，RX: 悬空输入） */
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
    (*read_port_) = ReadFun;
  }

  /* 可选：引脚重映射 */
  if (pin_remap != 0)
  {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(pin_remap, ENABLE);
  }

  /* 串口外设时钟使能 */
  if (CH32_UART_APB_MAP[id] == 1)
  {
    RCC_APB1PeriphClockCmd(CH32_UART_RCC_PERIPH_MAP[id], ENABLE);
  }
  else if (CH32_UART_APB_MAP[id] == 2)
  {
    RCC_APB2PeriphClockCmd(CH32_UART_RCC_PERIPH_MAP[id], ENABLE);
  }
  else
  {
    ASSERT(false);
  }
  RCC_AHBPeriphClockCmd(CH32_UART_RCC_PERIPH_MAP_DMA[id], ENABLE);

  // 3. USART 配置
  USART_InitTypeDef usart_cfg = {};
  usart_cfg.USART_BaudRate = config.baudrate;
  usart_cfg.USART_StopBits =
      (config.stop_bits == 2) ? USART_StopBits_2 : USART_StopBits_1;
  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      usart_cfg.USART_Parity = USART_Parity_No;
      usart_cfg.USART_WordLength = USART_WordLength_8b;
      break;
    case UART::Parity::EVEN:
      usart_cfg.USART_Parity = USART_Parity_Even;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      break;
    case UART::Parity::ODD:
      usart_cfg.USART_Parity = USART_Parity_Odd;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      break;
    default:
      ASSERT(false);
  }

  usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  usart_cfg.USART_Mode =
      (tx_enable ? USART_Mode_Tx : 0) | (rx_enable ? USART_Mode_Rx : 0);
  uart_mode_ = usart_cfg.USART_Mode;
  USART_Init(instance_, &usart_cfg);

  /* DMA 配置 */
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

    ch32_dma_register_callback(ch32_dma_get_id(CH32_UART_RX_DMA_CHANNEL_MAP[id]),
                               rx_cb_fun, this);

    DMA_DeInit(dma_rx_channel_);
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&instance_->DATAR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)rx_dma_model_.Buffer();
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

    ch32_dma_register_callback(ch32_dma_get_id(CH32_UART_TX_DMA_CHANNEL_MAP[id]),
                               tx_cb_fun, this);
    DMA_DeInit(dma_tx_channel_);
    dma_init.DMA_PeripheralBaseAddr = (u32)(&instance_->DATAR);
    dma_init.DMA_MemoryBaseAddr = 0;
    dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_BufferSize = 0;
    DMA_Init(dma_tx_channel_, &dma_init);
    DMA_ITConfig(dma_tx_channel_, DMA_IT_TC | DMA_IT_TE, ENABLE);
    USART_DMACmd(instance_, USART_DMAReq_Tx, ENABLE);
  }

  // 6. USART和相关中断
  USART_Cmd(instance_, ENABLE);

  if (rx_enable)
  {
    USART_ITConfig(instance_, USART_IT_IDLE, ENABLE);
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_rx_channel_)]);
  }

  if (tx_enable)
  {
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_tx_channel_)]);
  }

  NVIC_EnableIRQ(CH32_UART_IRQ_MAP[id]);
}

// Runtime USART configuration.
ErrorCode CH32UART::SetConfig(UART::Configuration config)
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

UartDmaControlResult CH32UART::ApplyPendingConfig(bool in_isr)
{
  if (!rx_config_gate_.TryEnterConfig())
  {
    return UartDmaControlResult::PENDING;
  }

  StopDataPath(in_isr);
  ApplyConfigPayload(in_isr);
  USART_Cmd(instance_, ENABLE);
  StartDataPath();
  return UartDmaControlResult::COMPLETED;
}

void CH32UART::ApplyConfigPayload(bool in_isr)
{
  const UART::Configuration config = requested_config_;

  USART_InitTypeDef usart_cfg = {};
  usart_cfg.USART_BaudRate = config.baudrate;
  usart_cfg.USART_StopBits =
      (config.stop_bits == 2) ? USART_StopBits_2 : USART_StopBits_1;

  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      usart_cfg.USART_Parity = USART_Parity_No;
      usart_cfg.USART_WordLength = USART_WordLength_8b;
      break;
    case UART::Parity::EVEN:
      usart_cfg.USART_Parity = USART_Parity_Even;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      break;
    case UART::Parity::ODD:
      usart_cfg.USART_Parity = USART_Parity_Odd;
      usart_cfg.USART_WordLength = USART_WordLength_9b;
      break;
    default:
      DEV_ASSERT_FROM_CALLBACK(false, in_isr);
      return;
  }

  usart_cfg.USART_Mode = uart_mode_;
  usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
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
  }
}

void CH32UART::StopDataPath(bool in_isr)
{
  SetDataPathInterrupts(false);

  if ((uart_mode_ & USART_Mode_Tx) != 0U)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, DISABLE);
    Ch32StopAndClearDmaChannel(dma_tx_channel_,
                               Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
  }
  if ((uart_mode_ & USART_Mode_Rx) != 0U)
  {
    USART_DMACmd(instance_, USART_DMAReq_Rx, DISABLE);
    Ch32StopAndClearDmaChannel(
        dma_rx_channel_, Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]), in_isr);
    (void)USART_GetFlagStatus(instance_, USART_FLAG_IDLE);
    (void)USART_ReceiveData(instance_);
  }
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

// Write callback (DMA-based transfer).
ErrorCode CH32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &CH32UART::_write_port);
  return uart->tx_dma_model_.Submit(uart->execution_policy_, in_isr);
}

void CH32UART::StartCircularDmaRx(uint8_t* data, size_t size)
{
  dma_rx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_rx_channel_->CNTR = size;
  Ch32UartIoFence();
  DMA_Cmd(dma_rx_channel_, ENABLE);
  Ch32UartIoFence();
  (void)dma_rx_channel_->CFGR;
}

UartDmaTxStartResult CH32UART::StartDmaTx(uint8_t* data, size_t size, int)
{
  const bool in_isr = InIsr();
  Ch32StopAndClearDmaChannel(dma_tx_channel_,
                             Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
  dma_tx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_tx_channel_->CNTR = size;
  Ch32UartIoFence();
  DMA_Cmd(dma_tx_channel_, ENABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((dma_tx_channel_->CFGR & DMA_CFGR1_EN) != 0U, in_isr);
  return UartDmaTxStartResult::STARTED;
}

// Read callback (interrupt-driven).
ErrorCode CH32UART::ReadFun(ReadPort&, bool)
{
  // 接收由 IDLE 中断驱动，读取在 ISR 中完成
  return ErrorCode::PENDING;
}

void CH32UART::OnRxDataAvailable(bool in_isr)
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

// USART IDLE interrupt handler.
extern "C" void ch32_uart_isr_handler_idle(ch32_uart_id_t id)
{
  auto uart = CH32UART::map_[id];
  if (!uart)
  {
    return;
  }
  uart->UartIRQHandler();
}

// DMA channel IRQ callbacks.
void CH32UART::TxDmaIRQHandler() { HandleNormalIrq(); }

// RX DMA IRQ entry. The scanner handles RX data and unified data-path errors.
void CH32UART::RxDmaIRQHandler() { HandleNormalIrq(); }

void CH32UART::UartIRQHandler() { HandleNormalIrq(); }

void CH32UART::HandleNormalIrq()
{
  ScanNormalIrqStatus(true);
  Ch32UartIoFence();
}

UartDmaControlResult CH32UART::RecoverDataPath(bool in_isr)
{
  if (!rx_config_gate_.TryEnterRecovery())
  {
    return UartDmaControlResult::PENDING;
  }

  StopDataPath(in_isr);
  StartDataPath();
  rx_config_gate_.LeaveRecovery();
  return UartDmaControlResult::COMPLETED;
}

void CH32UART::ScanNormalIrqStatus(bool in_isr)
{
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
    if (rx_enabled)
    {
      Ch32ClearDmaStatus(Ch32DmaGlobalStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]));
    }
    if (tx_enabled)
    {
      Ch32ClearDmaStatus(Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]));
    }
    if (uart_error)
    {
      (void)USART_ReceiveData(instance_);
    }

    tx_dma_model_.OnTransferError(execution_policy_, in_isr);
    return;
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

  if (tx_complete)
  {
    Ch32StopAndClearDmaChannel(dma_tx_channel_,
                               Ch32DmaGlobalStatus(CH32_UART_TX_DMA_IT_MAP[id_]), in_isr);
    tx_dma_model_.OnTransferDone(execution_policy_, in_isr);
  }

  if (rx_data_available)
  {
    OnRxDataAvailable(in_isr);
  }
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

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
