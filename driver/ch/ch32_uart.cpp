// ch32_uart.cpp

#include "ch32_uart.hpp"

#include "ch32_dma.hpp"
#include "ch32_gpio.hpp"

using namespace LibXR;

// === 静态对象指针表 ===
CH32UART *CH32UART::map[ch32_uart_id_t::CH32_UART_NUMBER] = {nullptr};

// === 构造函数：串口+DMA+GPIO初始化 ===
CH32UART::CH32UART(ch32_uart_id_t id, RawData dma_rx, RawData dma_tx,
                   GPIO_TypeDef *tx_gpio_port, uint16_t tx_gpio_pin,
                   GPIO_TypeDef *rx_gpio_port, uint16_t rx_gpio_pin, uint32_t pin_remap,
                   uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      id_(id),
      _read_port(dma_rx.size_),
      _write_port(tx_queue_size, dma_tx.size_ / 2),
      dma_buff_rx_(dma_rx),
      dma_buff_tx_(dma_tx),
      instance_(CH32_UART_GetInstanceID(id)),
      dma_rx_channel_(CH32_UART_RX_DMA_CHANNEL_MAP[id]),
      dma_tx_channel_(CH32_UART_TX_DMA_CHANNEL_MAP[id])
{
  map[id] = this;

  bool tx_enable = dma_tx.size_ > 1;
  bool rx_enable = dma_rx.size_ > 0;

  ASSERT(tx_enable || rx_enable);

  /* GPIO配置（TX: 推挽输出，RX: 悬空输入） */
  GPIO_InitTypeDef gpio_init = {};
  gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

  if (tx_enable)
  {
    (*write_port_) = WriteFun;
    gpio_init.GPIO_Pin = tx_gpio_pin;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(tx_gpio_port, &gpio_init);
    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(tx_gpio_port), ENABLE);
  }

  if (rx_enable)
  {
    (*read_port_) = ReadFun;
    gpio_init.GPIO_Pin = rx_gpio_pin;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(rx_gpio_port, &gpio_init);
    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(rx_gpio_port), ENABLE);
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
    DMA_DeInit(dma_rx_channel_);
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&instance_->DATAR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)dma_buff_rx_.addr_;
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_Mode = DMA_Mode_Circular;
    dma_init.DMA_BufferSize = dma_buff_rx_.size_;
    DMA_Init(dma_rx_channel_, &dma_init);
    DMA_Cmd(dma_rx_channel_, ENABLE);
    DMA_ITConfig(dma_rx_channel_, DMA_IT_TC, ENABLE);
    DMA_ITConfig(dma_rx_channel_, DMA_IT_HT, ENABLE);
    USART_DMACmd(instance_, USART_DMAReq_Rx, ENABLE);
  }

  if (tx_enable)
  {
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
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[CH32_DMA_GetID(dma_rx_channel_)]);
  }

  if (tx_enable)
  {
    NVIC_EnableIRQ(CH32_DMA_IRQ_MAP[CH32_DMA_GetID(dma_tx_channel_)]);
  }

  NVIC_EnableIRQ(CH32_UART_IRQ_MAP[id]);
}

// === 串口运行时配置变更 ===
ErrorCode CH32UART::SetConfig(UART::Configuration config)
{
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
      return ErrorCode::NOT_SUPPORT;
  }

  usart_cfg.USART_Mode = uart_mode_;
  usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_DeInit(instance_);
  USART_Init(instance_, &usart_cfg);

  if (uart_mode_ & USART_Mode_Rx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Rx, ENABLE);
    USART_ITConfig(instance_, USART_IT_IDLE, ENABLE);
  }

  if (uart_mode_ & USART_Mode_Tx)
  {
    USART_DMACmd(instance_, USART_DMAReq_Tx, ENABLE);
  }

  USART_Cmd(instance_, ENABLE);

  return ErrorCode::OK;
}

// === 写操作回调（DMA搬运） ===
ErrorCode CH32UART::WriteFun(WritePort &port)
{
  CH32UART *uart = CONTAINER_OF(&port, CH32UART, _write_port);
  if (!uart->dma_buff_tx_.HasPending())
  {
    WriteInfoBlock info;
    if (port.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    uint8_t *buffer = nullptr;
    bool use_pending = false;

    // DMA空闲判断
    bool dma_ready = uart->dma_tx_channel_->CNTR == 0;
    if (dma_ready)
    {
      buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer());
    }
    else
    {
      buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer());
      use_pending = true;
    }

    if (port.queue_data_->PopBatch(buffer, info.data.size_) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    if (use_pending)
    {
      uart->dma_buff_tx_.SetPendingLength(info.data.size_);
      uart->dma_buff_tx_.EnablePending();
      // 检查当前DMA是否可切换
      bool dma_ready = uart->dma_tx_channel_->CNTR == 0;
      if (dma_ready && uart->dma_buff_tx_.HasPending())
      {
        uart->dma_buff_tx_.Switch();
      }
      else
      {
        return ErrorCode::FAILED;
      }
    }

    port.queue_info_->Pop(uart->write_info_active_);

    DMA_Cmd(uart->dma_tx_channel_, DISABLE);
    uart->dma_tx_channel_->MADDR =
        reinterpret_cast<uint32_t>(uart->dma_buff_tx_.ActiveBuffer());
    uart->dma_tx_channel_->CNTR = info.data.size_;
    uart->_write_port.write_size_ = info.data.size_;
    DMA_Cmd(uart->dma_tx_channel_, ENABLE);

    uart->write_info_active_.op.UpdateStatus(false, ErrorCode::OK);

    return ErrorCode::FAILED;  // 实际是发起传输成功，但等待DMA完成
  }
  return ErrorCode::FAILED;
}

// === 读操作回调（由中断驱动） ===
ErrorCode CH32UART::ReadFun(ReadPort &port)
{
  UNUSED(port);
  // 接收由 IDLE 中断驱动，读取在 ISR 中完成
  return ErrorCode::EMPTY;
}

void CH32_UART_RX_ISR_Handler(LibXR::CH32UART *uart)
{
  auto rx_buf = static_cast<uint8_t *>(uart->dma_buff_rx_.addr_);
  size_t dma_size = uart->dma_buff_rx_.size_;
  size_t curr_pos = dma_size - uart->dma_rx_channel_->CNTR;
  size_t last_pos = uart->last_rx_pos_;

  if (curr_pos != last_pos)
  {
    if (curr_pos > last_pos)
    {
      // 普通区间
      uart->_read_port.queue_data_->PushBatch(&rx_buf[last_pos], curr_pos - last_pos);
    }
    else
    {
      // 回卷区
      uart->_read_port.queue_data_->PushBatch(&rx_buf[last_pos], dma_size - last_pos);
      uart->_read_port.queue_data_->PushBatch(&rx_buf[0], curr_pos);
    }
    uart->last_rx_pos_ = curr_pos;
    uart->_read_port.ProcessPendingReads(true);
  }
}

// === USART IDLE中断服务 ===
extern "C" void CH32_UART_ISR_Handler_IDLE(ch32_uart_id_t id)
{
  auto uart = CH32UART::map[id];
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

  CH32_UART_RX_ISR_Handler(uart);
}

// === DMA TX完成中断服务 ===
extern "C" void CH32_UART_ISR_Handler_TX_CPLT(ch32_uart_id_t id)
{
  auto uart = CH32UART::map[id];
  if (!uart)
  {
    return;
  }

  DMA_ClearITPendingBit(CH32_UART_TX_DMA_IT_MAP[id]);

  size_t pending_len = uart->dma_buff_tx_.GetPendingLength();

  if (pending_len == 0)
  {
    return;
  }

  uart->dma_buff_tx_.Switch();

  auto *buf = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer());
  DMA_Cmd(uart->dma_tx_channel_, DISABLE);
  uart->dma_tx_channel_->MADDR = (uint32_t)buf;
  uart->dma_tx_channel_->CNTR = pending_len;
  uart->_write_port.write_size_ = pending_len;
  DMA_Cmd(uart->dma_tx_channel_, ENABLE);

  WriteInfoBlock &current_info = uart->write_info_active_;

  // 有pending包，继续取下一包
  if (uart->_write_port.queue_info_->Pop(current_info) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  current_info.op.UpdateStatus(true, ErrorCode::OK);

  // 预装pending区
  WriteInfoBlock next_info;
  if (uart->write_port_->queue_info_->Peek(next_info) != ErrorCode::OK)
  {
    return;
  }

  if (uart->write_port_->queue_data_->PopBatch(
          reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer()),
          next_info.data.size_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  uart->dma_buff_tx_.SetPendingLength(next_info.data.size_);

  uart->dma_buff_tx_.EnablePending();
}

// === DMA 通道中断回调 ===
void CH32UART::TxDmaIRQHandler(DMA_Channel_TypeDef *channel, ch32_uart_id_t id)
{
  if (DMA_GetITStatus(CH32_UART_TX_DMA_IT_MAP[id]) == RESET) return;

  if (channel->CNTR == 0) CH32_UART_ISR_Handler_TX_CPLT(id);
}

void CH32UART::RxDmaIRQHandler(DMA_Channel_TypeDef *channel, ch32_uart_id_t id)
{
  UNUSED(channel);
  auto uart = CH32UART::map[id];
  if (!uart) return;

  if (DMA_GetITStatus(CH32_UART_RX_DMA_IT_HT_MAP[id]) == SET)
  {
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_HT_MAP[id]);
    CH32_UART_RX_ISR_Handler(uart);
  }

  if (DMA_GetITStatus(CH32_UART_RX_DMA_IT_TC_MAP[id]) == SET)
  {
    DMA_ClearITPendingBit(CH32_UART_RX_DMA_IT_TC_MAP[id]);
    CH32_UART_RX_ISR_Handler(uart);
  }
}

// === 各类串口中断入口适配 ===
#if defined(USART1)
extern "C" void USART1_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART1_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART1); }
#endif
#if defined(USART2)
extern "C" void USART2_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART2_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART2); }
#endif
#if defined(USART3)
extern "C" void USART3_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART3_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART3); }
#endif
#if defined(USART4)
extern "C" void USART4_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART4_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART4); }
#endif
#if defined(USART5)
extern "C" void USART5_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART5_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART5); }
#endif
#if defined(USART6)
extern "C" void USART6_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART6_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART6); }
#endif
#if defined(USART7)
extern "C" void USART7_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART7_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART7); }
#endif
#if defined(USART8)
extern "C" void USART8_IRQHandler(void) __attribute__((interrupt));
extern "C" void USART8_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_USART8); }
#endif
#if defined(UART1)
extern "C" void UART1_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART1_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART1); }
#endif
#if defined(UART2)
extern "C" void UART2_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART2_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART2); }
#endif
#if defined(UART3)
extern "C" void UART3_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART3_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART3); }
#endif
#if defined(UART4)
extern "C" void UART4_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART4_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART4); }
#endif
#if defined(UART5)
extern "C" void UART5_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART5_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART5); }
#endif
#if defined(UART6)
extern "C" void UART6_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART6_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART6); }
#endif
#if defined(UART7)
extern "C" void UART7_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART7_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART7); }
#endif
#if defined(UART8)
extern "C" void UART8_IRQHandler(void) __attribute__((interrupt));
extern "C" void UART8_IRQHandler(void) { CH32_UART_ISR_Handler_IDLE(CH32_UART8); }
#endif
