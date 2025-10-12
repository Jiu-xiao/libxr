#include "ch32_dma.hpp"

static struct
{
  ch32_dma_callback_t fun;
  void *arg;
} ch32_dma_callback_map[CH32_DMA_CHANNEL_NUMBER] = {};

void CH32_DMA_RegisterCallback(ch32_dma_channel_t id, ch32_dma_callback_t callback,
                               void *arg)
{
  ASSERT(id < CH32_DMA_CHANNEL_NUMBER);
  ch32_dma_callback_map[id] = {callback, arg};
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

#if defined(DMA1_Channel8)
  if (channel == DMA1_Channel8)
  {
    return CH32_DMA1_CHANNEL8;
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

#if defined(DMA1_Channel1)
extern "C" void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel1_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL1].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL1].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL1].arg);
  }
}
#endif

#if defined(DMA1_Channel2)
extern "C" void DMA1_Channel2_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel2_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL2].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL2].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL2].arg);
  }
}
#endif

#if defined(DMA1_Channel3)
extern "C" void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel3_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL3].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL3].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL3].arg);
  }
}
#endif

#if defined(DMA1_Channel4)
extern "C" void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel4_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL4].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL4].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL4].arg);
  }
}
#endif

#if defined(DMA1_Channel5)
extern "C" void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel5_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL5].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL5].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL5].arg);
  }
}
#endif

#if defined(DMA1_Channel6)
extern "C" void DMA1_Channel6_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel6_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL6].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL6].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL6].arg);
  }
}
#endif

#if defined(DMA1_Channel7)
extern "C" void DMA1_Channel7_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel7_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL7].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL7].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL7].arg);
  }
}
#endif

#if defined(DMA1_Channel8)
extern "C" void DMA1_Channel8_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA1_Channel8_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA1_CHANNEL8].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA1_CHANNEL8].fun(
        ch32_dma_callback_map[CH32_DMA1_CHANNEL8].arg);
  }
}
#endif

#if defined(DMA2_Channel1)
extern "C" void DMA2_Channel1_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel1_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL1].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL1].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL1].arg);
  }
}
#endif

#if defined(DMA2_Channel2)
extern "C" void DMA2_Channel2_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel2_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL2].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL2].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL2].arg);
  }
}
#endif

#if defined(DMA2_Channel3)
extern "C" void DMA2_Channel3_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel3_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL3].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL3].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL3].arg);
  }
}
#endif

#if defined(DMA2_Channel4)
extern "C" void DMA2_Channel4_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel4_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL4].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL4].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL4].arg);
  }
}
#endif

#if defined(DMA2_Channel5)
extern "C" void DMA2_Channel5_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel5_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL5].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL5].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL5].arg);
  }
}
#endif

#if defined(DMA2_Channel6)
extern "C" void DMA2_Channel6_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel6_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL6].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL6].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL6].arg);
  }
}
#endif

#if defined(DMA2_Channel7)
extern "C" void DMA2_Channel7_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel7_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL7].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL7].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL7].arg);
  }
}
#endif

#if defined(DMA2_Channel8)
extern "C" void DMA2_Channel8_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel8_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL8].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL8].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL8].arg);
  }
}
#endif

#if defined(DMA2_Channel9)
extern "C" void DMA2_Channel9_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel9_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL9].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL9].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL9].arg);
  }
}
#endif

#if defined(DMA2_Channel10)
extern "C" void DMA2_Channel10_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel10_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL10].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL10].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL10].arg);
  }
}
#endif

#if defined(DMA2_Channel11)
extern "C" void DMA2_Channel11_IRQHandler(void) __attribute__((interrupt));
extern "C" void DMA2_Channel11_IRQHandler(void)
{
  if (ch32_dma_callback_map[CH32_DMA2_CHANNEL11].fun != nullptr)
  {
    ch32_dma_callback_map[CH32_DMA2_CHANNEL11].fun(
        ch32_dma_callback_map[CH32_DMA2_CHANNEL11].arg);
  }
}
#endif
