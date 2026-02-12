#pragma once

#include "libxr.hpp"
#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

typedef enum
{
#if defined(SPI1)
  CH32_SPI1,
#endif
#if defined(SPI2)
  CH32_SPI2,
#endif
#if defined(SPI3)
  CH32_SPI3,
#endif
  CH32_SPI_NUMBER,
  CH32_SPI_ID_ERROR
} ch32_spi_id_t;

static constexpr uint8_t CH32_SPI_APB_MAP[] = {
#if defined(SPI1)
    2,
#endif
#if defined(SPI2)
    1,
#endif
#if defined(SPI3)
    1,
#endif
};

static constexpr uint32_t CH32_SPI_RCC_PERIPH_MAP[] = {
#if defined(SPI1)
    RCC_APB2Periph_SPI1,
#endif
#if defined(SPI2)
    RCC_APB1Periph_SPI2,
#endif
#if defined(SPI3)
    RCC_APB1Periph_SPI3,
#endif
};

static constexpr uint32_t CH32_SPI_RCC_PERIPH_MAP_DMA[] = {
#if defined(SPI1)
    RCC_AHBPeriph_DMA1,
#endif
#if defined(SPI2)
    RCC_AHBPeriph_DMA1,
#endif
#if defined(SPI3)
    RCC_AHBPeriph_DMA2,
#endif
};

static constexpr uint32_t CH32_SPI_TX_DMA_IT_MAP[] = {
#if defined(SPI1)
    DMA1_IT_TC3,
#endif
#if defined(SPI2)
    DMA1_IT_TC5,
#endif
#if defined(SPI3)
    DMA2_IT_TC2,
#endif
};

static constexpr uint32_t CH32_SPI_RX_DMA_IT_MAP[] = {
#if defined(SPI1)
    DMA1_IT_TC2,
#endif
#if defined(SPI2)
    DMA1_IT_TC4,
#endif
#if defined(SPI3)
    DMA2_IT_TC1,
#endif
};

static DMA_Channel_TypeDef* const CH32_SPI_TX_DMA_CHANNEL_MAP[] = {
#if defined(SPI1)
    DMA1_Channel3,
#endif
#if defined(SPI2)
    DMA1_Channel5,
#endif
#if defined(SPI3)
    DMA2_Channel2,
#endif
};

static DMA_Channel_TypeDef* const CH32_SPI_RX_DMA_CHANNEL_MAP[] = {
#if defined(SPI1)
    DMA1_Channel2,
#endif
#if defined(SPI2)
    DMA1_Channel4,
#endif
#if defined(SPI3)
    DMA2_Channel1,
#endif
};

static constexpr IRQn_Type CH32_SPI_IRQ_MAP[] = {
#if defined(SPI1)
    SPI1_IRQn,
#endif
#if defined(SPI2)
    SPI2_IRQn,
#endif
#if defined(SPI3)
    SPI3_IRQn,
#endif
};

ch32_spi_id_t ch32_spi_get_id(SPI_TypeDef* addr);
SPI_TypeDef* ch32_spi_get_instance_id(ch32_spi_id_t);
