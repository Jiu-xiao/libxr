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

STM32SPI::STM32SPI(SPI_HandleTypeDef *spi_handle, RawData dma_buff_rx,
                   RawData dma_buff_tx, uint32_t dma_enable_min_size)
    : SPI(),
      dma_buff_rx_(dma_buff_rx),
      dma_buff_tx_(dma_buff_tx),
      spi_handle_(spi_handle),
      id_(STM32_SPI_GetID(spi_handle_->Instance)),
      dma_enable_min_size_(dma_enable_min_size)
{
  ASSERT(id_ != STM32_SPI_ID_ERROR);

  map[id_] = this;
}

ErrorCode STM32SPI::ReadAndWrite(RawData read_data, ConstRawData write_data,
                                 OperationRW &op)
{
  uint32_t need_write = max(write_data.size_, read_data.size_);

  if (dma_buff_rx_.size_ > 0)
  {
    ASSERT(need_write <= dma_buff_rx_.size_);
  }

  if (dma_buff_tx_.size_ > 0)
  {
    ASSERT(need_write <= dma_buff_tx_.size_);
  }

  if (spi_handle_->State != HAL_SPI_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  mem_read_ = false;

  if (need_write > dma_enable_min_size_)
  {
    rw_op_ = op;

    if (write_data.size_ > 0)
    {
      memcpy(dma_buff_tx_.addr_, write_data.addr_, need_write);

#if __DCACHE_PRESENT
      SCB_CleanDCache_by_Addr(static_cast<uint32_t *>(dma_buff_tx_.addr_),
                              static_cast<int32_t>(write_data.size_));
#endif
    }
    if (read_data.size_ > 0)
    {
      read_buff_ = read_data;
    }

    if (write_data.size_ > 0 && read_data.size_ > 0)
    {
      HAL_SPI_TransmitReceive_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_), need_write);
    }
    else if (write_data.size_ > 0)
    {
      HAL_SPI_Transmit_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                           need_write);
    }
    else if (read_data.size_ > 0)
    {
      HAL_SPI_Receive_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_rx_.addr_),
                          need_write);
    }
    else
    {
      if (op.type != OperationRW::OperationType::BLOCK)
      {
        op.UpdateStatus(false, ErrorCode::OK);
      }
      return ErrorCode::OK;
    }

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    if (write_data.size_ > 0)
    {
      memcpy(dma_buff_tx_.addr_, write_data.addr_, write_data.size_);
    }

    ErrorCode ans = ErrorCode::OK;
    if (read_data.size_ > 0 && write_data.size_ > 0)
    {
      ans = HAL_SPI_TransmitReceive(
                spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                static_cast<uint8_t *>(dma_buff_rx_.addr_), need_write, 20) == HAL_OK
                ? ErrorCode::OK
                : ErrorCode::BUSY;
    }
    else if (read_data.size_ > 0)
    {
      ans = HAL_SPI_Receive(spi_handle_, static_cast<uint8_t *>(dma_buff_rx_.addr_),
                            need_write, 20) == HAL_OK
                ? ErrorCode::OK
                : ErrorCode::BUSY;
    }
    else if (write_data.size_ > 0)
    {
      ans = HAL_SPI_Transmit(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                             need_write, 20) == HAL_OK
                ? ErrorCode::OK
                : ErrorCode::BUSY;
    }
    else
    {
      if (op.type != OperationRW::OperationType::BLOCK)
      {
        op.UpdateStatus(false, ErrorCode::OK);
      }
      return ErrorCode::OK;
    }

    memcpy(read_data.addr_, dma_buff_rx_.addr_, read_data.size_);

    op.UpdateStatus(false, std::forward<ErrorCode>(ans));

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }

    return ans;
  }
}

ErrorCode STM32SPI::SetConfig(SPI::Configuration config)
{
  switch (config.clock_polarity)
  {
    case SPI::ClockPolarity::LOW:
      spi_handle_->Init.CLKPolarity = SPI_POLARITY_LOW;
      break;
    case SPI::ClockPolarity::HIGH:
      spi_handle_->Init.CLKPolarity = SPI_POLARITY_HIGH;
      break;
  }

  switch (config.clock_phase)
  {
    case SPI::ClockPhase::EDGE_1:
      spi_handle_->Init.CLKPhase = SPI_PHASE_1EDGE;
      break;
    case SPI::ClockPhase::EDGE_2:
      spi_handle_->Init.CLKPhase = SPI_PHASE_2EDGE;
      break;
  }

  return HAL_SPI_Init(spi_handle_) == HAL_OK ? ErrorCode::OK : ErrorCode::BUSY;
}

ErrorCode STM32SPI::MemRead(uint16_t reg, RawData read_data, OperationRW &op)
{
  uint32_t need_read = read_data.size_;

  if (spi_handle_->State != HAL_SPI_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  ASSERT(dma_buff_rx_.size_ >= need_read + 1);
  ASSERT(dma_buff_tx_.size_ >= need_read + 1);

  uint8_t *dma_buffer_rx = reinterpret_cast<uint8_t *>(dma_buff_rx_.addr_);
  uint8_t *dma_buffer_tx = reinterpret_cast<uint8_t *>(dma_buff_tx_.addr_);

  if (need_read + 1 > dma_enable_min_size_)
  {
    mem_read_ = true;
    memset(dma_buff_tx_.addr_, 0, need_read + 1);
    dma_buffer_tx[0] = reg | 0x80;
    rw_op_ = op;
    read_buff_ = read_data;

    HAL_SPI_TransmitReceive_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                static_cast<uint8_t *>(dma_buff_rx_.addr_),
                                need_read + 1);

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    mem_read_ = false;
    memset(dma_buff_tx_.addr_, 0, need_read + 1);
    dma_buffer_tx[0] = reg | 0x80;
    ErrorCode ans =
        HAL_SPI_TransmitReceive(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                static_cast<uint8_t *>(dma_buff_rx_.addr_), need_read + 1,
                                20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    memcpy(read_data.addr_, dma_buffer_rx + 1, read_data.size_);

    op.UpdateStatus(false, std::forward<ErrorCode>(ans));

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }

    return ans;
  }
}

ErrorCode STM32SPI::MemWrite(uint16_t reg, ConstRawData write_data, OperationRW &op)
{
  uint32_t need_write = write_data.size_;

  if (spi_handle_->State != HAL_SPI_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  mem_read_ = false;

  ASSERT(dma_buff_tx_.size_ >= need_write + 1);

  uint8_t *dma_buffer_tx = reinterpret_cast<uint8_t *>(dma_buff_tx_.addr_);

  if (need_write + 1 > dma_enable_min_size_)
  {
    memcpy(dma_buffer_tx + 1, write_data.addr_, need_write);
    *dma_buffer_tx = reg & 0x7f;

    rw_op_ = op;
    read_buff_ = {nullptr, 0};

    HAL_SPI_Transmit_DMA(spi_handle_, dma_buffer_tx, need_write + 1);

    op.MarkAsRunning();
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    memcpy(dma_buffer_tx + 1, write_data.addr_, need_write);
    *dma_buffer_tx = reg & 0x7f;
    ErrorCode ans =
        HAL_SPI_Transmit(spi_handle_, dma_buffer_tx, need_write + 1, 20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    op.UpdateStatus(false, std::forward<ErrorCode>(ans));

    if (op.type == OperationRW::OperationType::BLOCK)
    {
      return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
    }

    return ans;
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
