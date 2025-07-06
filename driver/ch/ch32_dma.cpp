#include "ch32_dma.hpp"

#include "ch32_uart.hpp"

/* UART/USART TX DMA */

extern "C" void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel7_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel2_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel5_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel4_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel6_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel8_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel10_IRQHandler(void) __attribute__((interrupt));

extern "C" void DMA1_Channel4_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA1_Channel4, ch32_uart_id_t::CH32_USART1);
}

extern "C" void DMA1_Channel7_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA1_Channel7, ch32_uart_id_t::CH32_USART2);
}

extern "C" void DMA1_Channel2_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA1_Channel2, ch32_uart_id_t::CH32_USART3);
}

extern "C" void DMA2_Channel5_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA2_Channel5, ch32_uart_id_t::CH32_UART4);
}

extern "C" void DMA2_Channel4_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA2_Channel4, ch32_uart_id_t::CH32_UART5);
}

extern "C" void DMA2_Channel6_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA2_Channel6, ch32_uart_id_t::CH32_UART6);
}

extern "C" void DMA2_Channel8_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA2_Channel8, ch32_uart_id_t::CH32_UART7);
}

extern "C" void DMA2_Channel10_IRQHandler(void)
{
  LibXR::CH32UART::TxDmaIRQHandler(DMA2_Channel10, ch32_uart_id_t::CH32_UART8);
}

/* UART/USART RX DMA */

extern "C" void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel6_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel3_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel2_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel7_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel9_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel11_IRQHandler(void) __attribute__((interrupt));

extern "C" void DMA1_Channel5_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA1_Channel5, ch32_uart_id_t::CH32_USART1);
}

extern "C" void DMA1_Channel6_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA1_Channel6, ch32_uart_id_t::CH32_USART2);
}

extern "C" void DMA1_Channel3_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA1_Channel3, ch32_uart_id_t::CH32_USART3);
}

extern "C" void DMA2_Channel3_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA2_Channel3, ch32_uart_id_t::CH32_UART4);
}

extern "C" void DMA2_Channel2_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA2_Channel2, ch32_uart_id_t::CH32_UART5);
}

extern "C" void DMA2_Channel7_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA2_Channel7, ch32_uart_id_t::CH32_UART6);
}

extern "C" void DMA2_Channel9_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA2_Channel9, ch32_uart_id_t::CH32_UART7);
}

extern "C" void DMA2_Channel11_IRQHandler(void)
{
  LibXR::CH32UART::RxDmaIRQHandler(DMA2_Channel11, ch32_uart_id_t::CH32_UART8);
}

ch32_dma_channel_t CH32_DMA_GetID(DMA_Channel_TypeDef *channel)
{
#if defined(DMA1_Channel1)
  if (channel == DMA1_Channel1)
  {
    return CH32_DMA1_CHANNEL1;
  }
#endif

#if defined(DMA1_Channel2)
  if (channel == DMA1_Channel2)
  {
    return CH32_DMA1_CHANNEL2;
  }
#endif

#if defined(DMA1_Channel3)
  if (channel == DMA1_Channel3)
  {
    return CH32_DMA1_CHANNEL3;
  }
#endif

#if defined(DMA1_Channel4)
  if (channel == DMA1_Channel4)
  {
    return CH32_DMA1_CHANNEL4;
  }
#endif

#if defined(DMA1_Channel5)
  if (channel == DMA1_Channel5)
  {
    return CH32_DMA1_CHANNEL5;
  }
#endif

#if defined(DMA1_Channel6)
  if (channel == DMA1_Channel6)
  {
    return CH32_DMA1_CHANNEL6;
  }
#endif

#if defined(DMA1_Channel7)
  if (channel == DMA1_Channel7)
  {
    return CH32_DMA1_CHANNEL7;
  }
#endif

#if defined(DMA2_Channel1)
  if (channel == DMA2_Channel1)
  {
    return CH32_DMA2_CHANNEL1;
  }
#endif

#if defined(DMA2_Channel2)
  if (channel == DMA2_Channel2)
  {
    return CH32_DMA2_CHANNEL2;
  }
#endif

#if defined(DMA2_Channel3)
  if (channel == DMA2_Channel3)
  {
    return CH32_DMA2_CHANNEL3;
  }
#endif

#if defined(DMA2_Channel4)
  if (channel == DMA2_Channel4)
  {
    return CH32_DMA2_CHANNEL4;
  }
#endif

#if defined(DMA2_Channel5)
  if (channel == DMA2_Channel5)
  {
    return CH32_DMA2_CHANNEL5;
  }
#endif

#if defined(DMA2_Channel6)
  if (channel == DMA2_Channel6)
  {
    return CH32_DMA2_CHANNEL6;
  }
#endif

#if defined(DMA2_Channel7)
  if (channel == DMA2_Channel7)
  {
    return CH32_DMA2_CHANNEL7;
  }
#endif

#if defined(DMA2_Channel8)
  if (channel == DMA2_Channel8)
  {
    return CH32_DMA2_CHANNEL8;
  }
#endif

#if defined(DMA2_Channel9)
  if (channel == DMA2_Channel9)
  {
    return CH32_DMA2_CHANNEL9;
  }
#endif

#if defined(DMA2_Channel10)
  if (channel == DMA2_Channel10)
  {
    return CH32_DMA2_CHANNEL10;
  }
#endif

#if defined(DMA2_Channel11)
  if (channel == DMA2_Channel11)
  {
    return CH32_DMA2_CHANNEL11;
  }
#endif

  return CH32_DMA_CHANNEL_NONE;
}

DMA_Channel_TypeDef *CH32_DMA_GetChannel(ch32_dma_channel_t id)
{
#if defined(DMA1_Channel1)
  if (id == CH32_DMA1_CHANNEL1)
  {
    return DMA1_Channel1;
  }
#endif

#if defined(DMA1_Channel2)
  if (id == CH32_DMA1_CHANNEL2)
  {
    return DMA1_Channel2;
  }
#endif

#if defined(DMA1_Channel3)
  if (id == CH32_DMA1_CHANNEL3)
  {
    return DMA1_Channel3;
  }
#endif

#if defined(DMA1_Channel4)
  if (id == CH32_DMA1_CHANNEL4)
  {
    return DMA1_Channel4;
  }
#endif

#if defined(DMA1_Channel5)
  if (id == CH32_DMA1_CHANNEL5)
  {
    return DMA1_Channel5;
  }
#endif

#if defined(DMA1_Channel6)
  if (id == CH32_DMA1_CHANNEL6)
  {
    return DMA1_Channel6;
  }
#endif

#if defined(DMA1_Channel7)
  if (id == CH32_DMA1_CHANNEL7)
  {
    return DMA1_Channel7;
  }
#endif

#if defined(DMA2_Channel1)
  if (id == CH32_DMA2_CHANNEL1)
  {
    return DMA2_Channel1;
  }
#endif

#if defined(DMA2_Channel2)
  if (id == CH32_DMA2_CHANNEL2)
  {
    return DMA2_Channel2;
  }
#endif

#if defined(DMA2_Channel3)
  if (id == CH32_DMA2_CHANNEL3)
  {
    return DMA2_Channel3;
  }
#endif

#if defined(DMA2_Channel4)
  if (id == CH32_DMA2_CHANNEL4)
  {
    return DMA2_Channel4;
  }
#endif

#if defined(DMA2_Channel5)
  if (id == CH32_DMA2_CHANNEL5)
  {
    return DMA2_Channel5;
  }
#endif

#if defined(DMA2_Channel6)
  if (id == CH32_DMA2_CHANNEL6)
  {
    return DMA2_Channel6;
  }
#endif

#if defined(DMA2_Channel7)
  if (id == CH32_DMA2_CHANNEL7)
  {
    return DMA2_Channel7;
  }
#endif

#if defined(DMA2_Channel8)
  if (id == CH32_DMA2_CHANNEL8)
  {
    return DMA2_Channel8;
  }
#endif

#if defined(DMA2_Channel9)
  if (id == CH32_DMA2_CHANNEL9)
  {
    return DMA2_Channel9;
  }
#endif

#if defined(DMA2_Channel10)
  if (id == CH32_DMA2_CHANNEL10)
  {
    return DMA2_Channel10;
  }
#endif

#if defined(DMA2_Channel11)
  if (id == CH32_DMA2_CHANNEL11)
  {
    return DMA2_Channel11;
  }
#endif

  return NULL;
}
