#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

typedef void (*ch32_dma_callback_t)(void *);

typedef enum
{
#if defined(DMA1_Channel1)
  CH32_DMA1_CHANNEL1,
#endif
#if defined(DMA1_Channel2)
  CH32_DMA1_CHANNEL2,
#endif
#if defined(DMA1_Channel3)
  CH32_DMA1_CHANNEL3,
#endif
#if defined(DMA1_Channel4)
  CH32_DMA1_CHANNEL4,
#endif
#if defined(DMA1_Channel5)
  CH32_DMA1_CHANNEL5,
#endif
#if defined(DMA1_Channel6)
  CH32_DMA1_CHANNEL6,
#endif
#if defined(DMA1_Channel7)
  CH32_DMA1_CHANNEL7,
#endif
#if defined(DMA1_Channel8)
  CH32_DMA1_CHANNEL8,
#endif
#if defined(DMA2_Channel1)
  CH32_DMA2_CHANNEL1,
#endif
#if defined(DMA2_Channel2)
  CH32_DMA2_CHANNEL2,
#endif
#if defined(DMA2_Channel3)
  CH32_DMA2_CHANNEL3,
#endif
#if defined(DMA2_Channel4)
  CH32_DMA2_CHANNEL4,
#endif
#if defined(DMA2_Channel5)
  CH32_DMA2_CHANNEL5,
#endif
#if defined(DMA2_Channel6)
  CH32_DMA2_CHANNEL6,
#endif
#if defined(DMA2_Channel7)
  CH32_DMA2_CHANNEL7,
#endif
#if defined(DMA2_Channel8)
  CH32_DMA2_CHANNEL8,
#endif
#if defined(DMA2_Channel9)
  CH32_DMA2_CHANNEL9,
#endif
#if defined(DMA2_Channel10)
  CH32_DMA2_CHANNEL10,
#endif
#if defined(DMA2_Channel11)
  CH32_DMA2_CHANNEL11,
#endif

  CH32_DMA_CHANNEL_NUMBER,
  CH32_DMA_CHANNEL_NONE
} ch32_dma_channel_t;

static constexpr IRQn_Type CH32_DMA_IRQ_MAP[] = {
#if defined(DMA1_Channel1)
    DMA1_Channel1_IRQn,
#endif
#if defined(DMA1_Channel2)
    DMA1_Channel2_IRQn,
#endif
#if defined(DMA1_Channel3)
    DMA1_Channel3_IRQn,
#endif
#if defined(DMA1_Channel4)
    DMA1_Channel4_IRQn,
#endif
#if defined(DMA1_Channel5)
    DMA1_Channel5_IRQn,
#endif
#if defined(DMA1_Channel6)
    DMA1_Channel6_IRQn,
#endif
#if defined(DMA1_Channel7)
    DMA1_Channel7_IRQn,
#endif
#if defined(DMA2_Channel1)
    DMA2_Channel1_IRQn,
#endif
#if defined(DMA2_Channel2)
    DMA2_Channel2_IRQn,
#endif
#if defined(DMA2_Channel3)
    DMA2_Channel3_IRQn,
#endif
#if defined(DMA2_Channel4)
    DMA2_Channel4_IRQn,
#endif
#if defined(DMA2_Channel5)
    DMA2_Channel5_IRQn,
#endif
#if defined(DMA2_Channel6)
    DMA2_Channel6_IRQn,
#endif
#if defined(DMA2_Channel7)
    DMA2_Channel7_IRQn,
#endif
#if defined(DMA2_Channel8)
    DMA2_Channel8_IRQn,
#endif
#if defined(DMA2_Channel9)
    DMA2_Channel9_IRQn,
#endif
#if defined(DMA2_Channel10)
    DMA2_Channel10_IRQn,
#endif
#if defined(DMA2_Channel11)
    DMA2_Channel11_IRQn,
#endif
};

ch32_dma_channel_t CH32_DMA_GetID(DMA_Channel_TypeDef *channel);

DMA_Channel_TypeDef *CH32_DMA_GetChannel(ch32_dma_channel_t id);

void CH32_DMA_RegisterCallback(ch32_dma_channel_t id, ch32_dma_callback_t callback,
                               void *arg);
