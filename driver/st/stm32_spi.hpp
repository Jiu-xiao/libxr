#pragma once

#include <cstring>

#include "main.h"

#ifdef HAL_SPI_MODULE_ENABLED

#ifdef SPI
#undef SPI
#endif

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "spi.hpp"

typedef enum
{
#ifdef SPI1
  STM32_SPI1,
#endif
#ifdef SPI2
  STM32_SPI2,
#endif
#ifdef SPI3
  STM32_SPI3,
#endif
#ifdef SPI4
  STM32_SPI4,
#endif
#ifdef SPI5
  STM32_SPI5,
#endif
#ifdef SPI6
  STM32_SPI6,
#endif
#ifdef SPI7
  STM32_SPI7,
#endif
#ifdef SPI8
  STM32_SPI8,
#endif
  STM32_SPI_NUMBER,
  STM32_SPI_ID_ERROR
} stm32_spi_id_t;

stm32_spi_id_t STM32_SPI_GetID(SPI_TypeDef *addr);  // NOLINT

namespace LibXR
{
class STM32SPI : public SPI
{
 public:
  STM32SPI(SPI_HandleTypeDef *spi_handle, RawData dma_buff_rx, RawData dma_buff_tx,
           uint32_t dma_enable_min_size = 3);

  ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data, OperationRW &op,
                         bool in_isr) override;

  ErrorCode SetConfig(SPI::Configuration config) override;

  ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW &op,
                    bool in_isr) override;

  ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW &op,
                     bool in_isr) override;

  uint32_t GetMaxBusSpeed() const override;

  Prescaler GetMaxPrescaler() const override;

  ErrorCode Transfer(size_t size, OperationRW &op, bool in_isr) override;

  SPI_HandleTypeDef *spi_handle_;

  stm32_spi_id_t id_ = STM32_SPI_ID_ERROR;

  uint32_t dma_enable_min_size_ = 3;

  OperationRW rw_op_;

  RawData read_buff_;

  bool mem_read_ = false;

  static STM32SPI *map[STM32_SPI_NUMBER];  // NOLINT
};

}  // namespace LibXR

#endif
