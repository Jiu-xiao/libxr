// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_uart.cpp

#include "ch32_uart.hpp"

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

namespace
{

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
    ASSERT(CH32_UART_TX_DMA_IT_TE_MAP[id] != 0);
    ASSERT(CH32_UART_TX_DMA_IT_GL_MAP[id] != 0);
  }
  if (rx_enable)
  {
    ASSERT(dma_rx_channel_ != nullptr);
    ASSERT(CH32_UART_RX_DMA_IT_TC_MAP[id] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_HT_MAP[id] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_TE_MAP[id] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_GL_MAP[id] != 0);
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

  requested_config_.Store(config);
  tx_dma_model_.RequestConfig(InIsr());
  return ErrorCode::OK;
}

bool CH32UART::ApplyPendingConfig(bool in_isr)
{
  if (!hardware_gate_.TryEnterConfig())
  {
    return false;
  }

  MaskNormalIrqs();

  if (uart_mode_ & USART_Mode_Tx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, DISABLE);
    Ch32StopAndClearDmaChannel(dma_tx_channel_, CH32_UART_TX_DMA_IT_GL_MAP[id_], in_isr);
  }
  if (uart_mode_ & USART_Mode_Rx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Rx, DISABLE);
    Ch32StopAndClearDmaChannel(dma_rx_channel_, CH32_UART_RX_DMA_IT_GL_MAP[id_], in_isr);
    if (USART_GetFlagStatus(instance_, USART_FLAG_IDLE) != RESET)
    {
      (void)USART_ReceiveData(instance_);
    }
  }

  hardware_gate_.ConsumePendingConfig();
  ApplyConfigPayload(in_isr);

  if (uart_mode_ & USART_Mode_Rx)
  {
    rx_dma_model_.Start(*this);
    USART_DMACmd(instance_, USART_DMAReq_Rx, ENABLE);
    USART_ITConfig(instance_, USART_IT_IDLE, ENABLE);
  }
  if (uart_mode_ & USART_Mode_Tx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, ENABLE);
  }

  USART_Cmd(instance_, ENABLE);
  return true;
}

bool CH32UART::OnConfigApplied(bool in_isr)
{
  RestoreNormalIrqs();
  Ch32UartIoFence();
  DispatchHardwareActions(hardware_gate_.LeaveConfig(), in_isr);
  return true;
}

void CH32UART::ApplyConfigPayload(bool in_isr)
{
  UART::Configuration config{};
  (void)requested_config_.LoadLatest(config);

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

// Write callback (DMA-based transfer).
ErrorCode CH32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &CH32UART::_write_port);
  return uart->tx_dma_model_.Submit(in_isr);
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

UartDmaTxStartResult CH32UART::StartDmaTx(
    uint8_t* data, size_t size, int, UartHardwareGate::OwnerContext* hardware_context)
{
  const bool in_isr = InIsr();
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
    Ch32UartIoFence();
    if (nested_start)
    {
      hardware_gate_.LeaveNestedTxStart(*hardware_context);
    }
    else
    {
      DispatchHardwareActions(hardware_gate_.LeaveTxStart(), in_isr);
    }
    return result;
  };

  Ch32StopAndClearDmaChannel(dma_tx_channel_, CH32_UART_TX_DMA_IT_GL_MAP[id_], in_isr);
  dma_tx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_tx_channel_->CNTR = size;
  Ch32UartIoFence();
  DMA_Cmd(dma_tx_channel_, ENABLE);
  Ch32UartIoFence();
  DEV_ASSERT_FROM_CALLBACK((dma_tx_channel_->CFGR & DMA_CFGR1_EN) != 0U, in_isr);
  return finish_start(UartDmaTxStartResult::STARTED);
}

// Read callback (interrupt-driven).
ErrorCode CH32UART::ReadFun(ReadPort&, bool)
{
  // 接收由 IDLE 中断驱动，读取在 ISR 中完成
  return ErrorCode::PENDING;
}

void CH32UART::OnRxDataAvailable(bool in_isr)
{
  rx_dma_model_.OnDataAvailable(*this, _read_port, in_isr);
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

// RX DMA IRQ entry. The shared scanner checks the complete UART IRQ domain.
void CH32UART::RxDmaIRQHandler() { HandleNormalIrq(); }

void CH32UART::UartIRQHandler() { HandleNormalIrq(); }

void CH32UART::HandleNormalIrq()
{
  UartHardwareGate::OwnerContext hardware_context;
  if (!hardware_gate_.TryEnterIrq(hardware_context))
  {
    DeferNormalIrq(true);
    return;
  }

  ScanNormalIrqStatus(true, hardware_context);
  Ch32UartIoFence();
  DispatchHardwareActions(hardware_gate_.LeaveIrq(hardware_context), true);
}

void CH32UART::DeferNormalIrq(bool in_isr)
{
  MaskNormalIrqs();
  hardware_gate_.MarkIrqDeferred();
  DispatchHardwareActions(UartHardwareGate::PendingAction::IRQ_DEFERRED, in_isr);
}

void CH32UART::ScanNormalIrqStatus(bool in_isr,
                                   UartHardwareGate::OwnerContext& hardware_context)
{
  const bool rx_enabled = (uart_mode_ & USART_Mode_Rx) != 0U;
  const bool tx_enabled = (uart_mode_ & USART_Mode_Tx) != 0U;
  const bool rx_error =
      rx_enabled && (DMA_GetITStatus(CH32_UART_RX_DMA_IT_TE_MAP[id_]) == SET);
  const bool rx_half =
      rx_enabled && (DMA_GetITStatus(CH32_UART_RX_DMA_IT_HT_MAP[id_]) == SET);
  const bool rx_complete =
      rx_enabled && (DMA_GetITStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]) == SET);
  const bool tx_error =
      tx_enabled && (DMA_GetITStatus(CH32_UART_TX_DMA_IT_TE_MAP[id_]) == SET);
  const bool tx_complete =
      tx_enabled && (DMA_GetITStatus(CH32_UART_TX_DMA_IT_MAP[id_]) == SET);

  if (rx_error)
  {
    Ch32StopAndClearDmaChannel(dma_rx_channel_, CH32_UART_RX_DMA_IT_GL_MAP[id_], in_isr);
    tx_dma_model_.RequestConfig(in_isr);
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

  if (rx_data_available)
  {
    OnRxDataAvailable(in_isr);
  }

  if (tx_error)
  {
    Ch32StopAndClearDmaChannel(dma_tx_channel_, CH32_UART_TX_DMA_IT_GL_MAP[id_], in_isr);
    tx_dma_model_.OnTransferError(in_isr, hardware_context);
  }
  else if (tx_complete)
  {
    Ch32StopAndClearDmaChannel(dma_tx_channel_, CH32_UART_TX_DMA_IT_GL_MAP[id_], in_isr);
    tx_dma_model_.OnTransferDone(in_isr, hardware_context);
  }
}

void CH32UART::MaskNormalIrqs()
{
  NVIC_DisableIRQ(CH32_UART_IRQ_MAP[id_]);
  if (uart_mode_ & USART_Mode_Tx)
  {
    NVIC_DisableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_tx_channel_)]);
  }
  if (uart_mode_ & USART_Mode_Rx)
  {
    NVIC_DisableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_rx_channel_)]);
  }
  Ch32UartIoFence();
}

void CH32UART::RestoreNormalIrqs()
{
  NVIC_EnableIRQ(CH32_UART_IRQ_MAP[id_]);
  if (uart_mode_ & USART_Mode_Tx)
  {
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_tx_channel_)]);
  }
  if (uart_mode_ & USART_Mode_Rx)
  {
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[ch32_dma_get_id(dma_rx_channel_)]);
  }
  Ch32UartIoFence();
}

void CH32UART::DispatchHardwareActions(UartHardwareGate::PendingAction actions,
                                       bool in_isr)
{
  while (true)
  {
    if (UartHardwareGate::HasAction(actions, UartHardwareGate::PendingAction::CONFIG))
    {
      tx_dma_model_.ResumeConfig(in_isr);
      return;
    }

    if (UartHardwareGate::HasAction(actions,
                                    UartHardwareGate::PendingAction::IRQ_DEFERRED))
    {
      UartHardwareGate::OwnerContext hardware_context;
      if (!hardware_gate_.TryEnterDeferredIrq(hardware_context))
      {
        return;
      }

      MaskNormalIrqs();
      ScanNormalIrqStatus(in_isr, hardware_context);
      RestoreNormalIrqs();
      Ch32UartIoFence();
      actions = hardware_gate_.LeaveIrq(hardware_context);
      continue;
    }

    if (UartHardwareGate::HasAction(actions, UartHardwareGate::PendingAction::TX_START))
    {
      tx_dma_model_.ResumeStart(in_isr);
    }
    return;
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
