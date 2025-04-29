#pragma once

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
           uint32_t dma_enable_min_size = 3)
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

  ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data,
                         OperationRW &op) override
  {
    uint32_t need_write = max(write_data.size_, read_data.size_);
    ASSERT(need_write <= dma_buff_rx_.size_ && need_write <= dma_buff_tx_.size_);

    if (spi_handle_->State != HAL_SPI_STATE_READY)
    {
      return ErrorCode::BUSY;
    }

    mem_read_ = false;

    if (need_write > dma_enable_min_size_)
    {
      memcpy(dma_buff_tx_.addr_, write_data.addr_, need_write);
      memset(dma_buff_rx_.addr_, 0, need_write);
      rw_op_ = op;
      read_buff_ = read_data;

      HAL_SPI_TransmitReceive_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_), need_write);

      op.MarkAsRunning();
      if (op.type == OperationRW::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }
      return ErrorCode::OK;
    }
    else
    {
      memcpy(dma_buff_tx_.addr_, write_data.addr_, write_data.size_);
      memset(dma_buff_rx_.addr_, 0, write_data.size_);
      ErrorCode ans =
          HAL_SPI_TransmitReceive(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_), need_write,
                                  20) == HAL_OK
              ? ErrorCode::OK
              : ErrorCode::BUSY;

      memcpy(read_data.addr_, dma_buff_rx_.addr_, read_data.size_);

      op.UpdateStatus(false, std::forward<ErrorCode>(ans));

      if (op.type == OperationRW::OperationType::BLOCK)
      {
        return op.data.sem_info.sem->Wait(op.data.sem_info.timeout);
      }

      return ans;
    }
  }

  ErrorCode SetConfig(SPI::Configuration config) override
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

  ErrorCode MemRead(uint16_t reg, RawData read_data, OperationRW &op) override
  {
    uint32_t need_read = read_data.size_;

    if (spi_handle_->State != HAL_SPI_STATE_READY)
    {
      return ErrorCode::BUSY;
    }

    mem_read_ = false;

    uint8_t *dma_buffer_rx = reinterpret_cast<uint8_t *>(dma_buff_rx_.addr_);
    uint8_t *dma_buffer_tx = reinterpret_cast<uint8_t *>(dma_buff_tx_.addr_);

    if (need_read + 1 > dma_enable_min_size_)
    {
      memset(dma_buff_rx_.addr_, 0, need_read + 1);
      memset(dma_buff_tx_.addr_, 0, need_read + 1);
      dma_buffer_tx[0] = reg | 0x80;
      rw_op_ = op;
      read_buff_ = read_data;

      mem_read_ = true;

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
      memset(dma_buff_rx_.addr_, 0, need_read + 1);
      memset(dma_buff_tx_.addr_, 0, need_read + 1);
      dma_buffer_tx[0] = reg | 0x80;
      ErrorCode ans =
          HAL_SPI_TransmitReceive(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_),
                                  need_read + 1, 20) == HAL_OK
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

  ErrorCode MemWrite(uint16_t reg, ConstRawData write_data, OperationRW &op) override
  {
    uint32_t need_write = write_data.size_;

    if (spi_handle_->State != HAL_SPI_STATE_READY)
    {
      return ErrorCode::BUSY;
    }

    mem_read_ = false;

    uint8_t *dma_buffer_tx = reinterpret_cast<uint8_t *>(dma_buff_tx_.addr_);

    if (need_write + 1 > dma_enable_min_size_)
    {
      memcpy(dma_buffer_tx + 1, write_data.addr_, need_write);
      *dma_buffer_tx = reg & 0x7f;

      rw_op_ = op;
      read_buff_ = {nullptr, 0};

      HAL_SPI_TransmitReceive_DMA(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_),
                                  need_write + 1);

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
          HAL_SPI_TransmitReceive(spi_handle_, static_cast<uint8_t *>(dma_buff_tx_.addr_),
                                  static_cast<uint8_t *>(dma_buff_rx_.addr_),
                                  need_write + 1, 20) == HAL_OK
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

  RawData dma_buff_rx_, dma_buff_tx_;

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
