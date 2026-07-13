// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
// ch32_uart.cpp

#include "ch32_uart.hpp"

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

// Static instance map.
CH32UART* CH32UART::map_[ch32_uart_id_t::CH32_UART_NUMBER] = {nullptr};

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
  }
  if (rx_enable)
  {
    ASSERT(dma_rx_channel_ != nullptr);
    ASSERT(CH32_UART_RX_DMA_IT_TC_MAP[id] != 0);
    ASSERT(CH32_UART_RX_DMA_IT_HT_MAP[id] != 0);
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
    rx_dma_model_.Start(*this);
    DMA_ITConfig(dma_rx_channel_, DMA_IT_TC, ENABLE);
    DMA_ITConfig(dma_rx_channel_, DMA_IT_HT, ENABLE);
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
    DMA_ITConfig(dma_tx_channel_, DMA_IT_TC, ENABLE);
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
  tx_dma_model_.RequestConfig(false);
  return ErrorCode::OK;
}

bool CH32UART::ApplyPendingConfig(bool in_isr)
{
  UNUSED(in_isr);
  if (!rx_config_gate_.TryEnterConfig())
  {
    return false;
  }

  UART::Configuration config{};
  (void)requested_config_.LoadLatest(config);

  if (uart_mode_ & USART_Mode_Tx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, DISABLE);
    DMA_Cmd(dma_tx_channel_, DISABLE);
    DMA_ClearITPendingBit(CH32_UART_TX_DMA_IT_MAP[id_]);
  }
  if (uart_mode_ & USART_Mode_Rx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Rx, DISABLE);
    DMA_Cmd(dma_rx_channel_, DISABLE);
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_HT_MAP[id_]);
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_TC_MAP[id_]);
  }

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
      return true;
  }

  usart_cfg.USART_Mode = uart_mode_;
  usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_DeInit(instance_);
  USART_Init(instance_, &usart_cfg);

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

// Write callback (DMA-based transfer).
ErrorCode CH32UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &CH32UART::_write_port);
  return uart->tx_dma_model_.Submit(in_isr);
}

bool CH32UART::StartDmaTx(uint8_t* data, size_t size, int)
{
  DMA_Cmd(dma_tx_channel_, DISABLE);
  dma_tx_channel_->MADDR = reinterpret_cast<uint32_t>(data);
  dma_tx_channel_->CNTR = size;
  DMA_Cmd(dma_tx_channel_, ENABLE);
  return true;
}

// Read callback (interrupt-driven).
ErrorCode CH32UART::ReadFun(ReadPort&, bool)
{
  // 接收由 IDLE 中断驱动，读取在 ISR 中完成
  return ErrorCode::PENDING;
}

void ch32_uart_rx_isr_handler(LibXR::CH32UART* uart) { uart->OnRxDataAvailable(true); }

void CH32UART::OnRxDataAvailable(bool in_isr)
{
  if (!rx_config_gate_.TryEnterRx())
  {
    return;
  }

  rx_dma_model_.OnDataAvailable(*this, _read_port, in_isr);
  if (rx_config_gate_.LeaveRx())
  {
    tx_dma_model_.RequestConfig(in_isr);
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

  // 检查和清除IDLE标志
  if (!USART_GetITStatus(uart->instance_, USART_IT_IDLE))
  {
    return;
  }

  USART_ReceiveData(uart->instance_);

  ch32_uart_rx_isr_handler(uart);
}

// DMA TX completion interrupt handler.
extern "C" void ch32_uart_isr_handler_tx_cplt(CH32UART* uart)
{
  DMA_ClearITPendingBit(CH32_UART_TX_DMA_IT_MAP[uart->id_]);
  uart->tx_dma_model_.OnTransferDone(true);
}

// DMA channel IRQ callbacks.
void CH32UART::TxDmaIRQHandler()
{
  if (DMA_GetITStatus(CH32_UART_TX_DMA_IT_MAP[id_]) == RESET)
  {
    return;
  }

  if (dma_tx_channel_->CNTR == 0)
  {
    ch32_uart_isr_handler_tx_cplt(this);
  }
}

/**
 * @brief  DMA中断处理函数
 *
 * 如果DMA中断触发，且中断状态为半满或传输完成，清除中断标志，
 * 并调用对应的中断处理函数
 *
 * @param[in] id  UART的ID
 */
void CH32UART::RxDmaIRQHandler()
{
  if (DMA_GetITStatus(CH32_UART_RX_DMA_IT_HT_MAP[id_]) == SET)
  {
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_HT_MAP[id_]);
    ch32_uart_rx_isr_handler(this);
  }

  if (DMA_GetITStatus(CH32_UART_RX_DMA_IT_TC_MAP[id_]) == SET)
  {
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_TC_MAP[id_]);
    ch32_uart_rx_isr_handler(this);
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
