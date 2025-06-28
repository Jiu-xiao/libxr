#include "stm32_spi.hpp"

#include "libxr_def.hpp"


#ifdef HAL_SPI_MODULE_ENABLED

using namespace LibXR;

STM32SPI *STM32SPI::map[STM32_SPI_NUMBER] = {nullptr};

stm32_spi_id_t STM32_SPI_GetID(SPI_TypeDef *addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI_ID_ERROR;
  }
#ifdef SPI1
  else if (addr == SPI1)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI1;
  }
#endif
#ifdef SPI2
  else if (addr == SPI2)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI2;
  }
#endif
#ifdef SPI3
  else if (addr == SPI3)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI3;
  }
#endif
#ifdef SPI4
  else if (addr == SPI4)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI4;
  }
#endif
#ifdef SPI5
  else if (addr == SPI5)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI5;
  }
#endif
#ifdef SPI6
  else if (addr == SPI6)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI6;
  }
#endif
#ifdef SPI7
  else if (addr == SPI7)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI7;
  }
#endif
#ifdef SPI8
  else if (addr == SPI8)
  {  // NOLINT
    return stm32_spi_id_t::STM32_SPI8;
  }
#endif
  else
  {
    return stm32_spi_id_t::STM32_SPI_ID_ERROR;
  }
}

extern "C" void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  STM32SPI *spi = STM32SPI::map[STM32_SPI_GetID(hspi->Instance)];
  spi->rw_op_.UpdateStatus(true, ErrorCode::OK);
}

extern "C" void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  STM32SPI *spi = STM32SPI::map[STM32_SPI_GetID(hspi->Instance)];

  if (spi->read_buff_.size_ > 0)
  {
#if __DCACHE_PRESENT
    SCB_InvalidateDCache_by_Addr(spi->dma_buff_rx_.addr_, spi->read_buff_.size_);
#endif
    if (!spi->mem_read_)
    {
      memcpy(spi->read_buff_.addr_, spi->dma_buff_rx_.addr_, spi->read_buff_.size_);
    }
    else
    {
      uint8_t *rx_dma_buff = reinterpret_cast<uint8_t *>(spi->dma_buff_rx_.addr_);
      memcpy(spi->read_buff_.addr_, rx_dma_buff + 1, spi->read_buff_.size_);
    }
  }

  spi->rw_op_.UpdateStatus(true, ErrorCode::OK);
}

extern "C" void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  HAL_SPI_RxCpltCallback(hspi);
}

extern "C" void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  STM32SPI *spi = STM32SPI::map[STM32_SPI_GetID(hspi->Instance)];
  spi->rw_op_.UpdateStatus(false, ErrorCode::FAILED);
}

#endif
