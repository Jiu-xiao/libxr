#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

typedef enum
{
#if defined(I2C1)
  CH32_I2C1,
#endif
#if defined(I2C2)
  CH32_I2C2,
#endif
  CH32_I2C_NUMBER,
  CH32_I2C_ID_ERROR
} ch32_i2c_id_t;

static constexpr uint32_t CH32_I2C_RCC_PERIPH_MAP[] = {
#if defined(I2C1)
    RCC_APB1Periph_I2C1,
#endif
#if defined(I2C2)
    RCC_APB1Periph_I2C2,
#endif
};

static constexpr uint32_t CH32_I2C_RCC_PERIPH_MAP_DMA[] = {
#if defined(I2C1)
    RCC_AHBPeriph_DMA1,
#endif
#if defined(I2C2)
    RCC_AHBPeriph_DMA1,
#endif
};

// 仍按“类 F1”的常见映射：I2C1_TX=CH6, I2C1_RX=CH7; I2C2_TX=CH4, I2C2_RX=CH5
static DMA_Channel_TypeDef *const CH32_I2C_TX_DMA_CHANNEL_MAP[] = {
#if defined(I2C1)
    DMA1_Channel6,
#endif
#if defined(I2C2)
    DMA1_Channel4,
#endif
};

static DMA_Channel_TypeDef *const CH32_I2C_RX_DMA_CHANNEL_MAP[] = {
#if defined(I2C1)
    DMA1_Channel7,
#endif
#if defined(I2C2)
    DMA1_Channel5,
#endif
};

static constexpr uint32_t CH32_I2C_TX_DMA_IT_MAP[] = {
#if defined(I2C1)
    DMA1_IT_TC6,
#endif
#if defined(I2C2)
    DMA1_IT_TC4,
#endif
};

static constexpr uint32_t CH32_I2C_RX_DMA_IT_MAP[] = {
#if defined(I2C1)
    DMA1_IT_TC7,
#endif
#if defined(I2C2)
    DMA1_IT_TC5,
#endif
};

static constexpr IRQn_Type CH32_I2C_ER_IRQ_MAP[] = {
#if defined(I2C1)
    I2C1_ER_IRQn,
#endif
#if defined(I2C2)
    I2C2_ER_IRQn,
#endif
};

ch32_i2c_id_t CH32_I2C_GetID(I2C_TypeDef *addr);
I2C_TypeDef *CH32_I2C_GetInstanceID(ch32_i2c_id_t id);
