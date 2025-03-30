#pragma once

#include "main.h"

#ifdef HAL_I2C_MODULE_ENABLED

#ifdef I2C
#undef I2C
#endif

#include "i2c.hpp"
#include "libxr.hpp"

typedef enum
{
#ifdef I2C1
  STM32_I2C1,
#endif
#ifdef I2C2
  STM32_I2C2,
#endif
#ifdef I2C3
  STM32_I2C3,
#endif
#ifdef I2C4
  STM32_I2C4,
#endif
#ifdef I2C5
  STM32_I2C5,
#endif
#ifdef I2C6
  STM32_I2C6,
#endif
#ifdef I2C7
  STM32_I2C7,
#endif
#ifdef I2C8
  STM32_I2C8,
#endif
  STM32_I2C_NUMBER,
  STM32_I2C_ID_ERROR
} stm32_i2c_id_t;

stm32_i2c_id_t STM32_I2C_GetID(I2C_TypeDef *hi2c);  // NOLINT

namespace LibXR
{
class STM32I2C : public I2C
{
 public:
  STM32I2C(I2C_HandleTypeDef *hi2c, RawData dma_buff, uint32_t dma_enable_min_size = 3)
      : I2C(),
        id_(STM32_I2C_GetID(hi2c->Instance)),
        i2c_handle_(hi2c),
        dma_enable_min_size_(dma_enable_min_size),
        dma_buff_(dma_buff)
  {
    map[id_] = this;
  }

  ErrorCode Read(uint16_t slave_addr, RawData read_data, ReadOperation &op) override
  {
    if (i2c_handle_->State != HAL_I2C_STATE_READY)
    {
      return ErrorCode::BUSY;
    }

    if (read_data.size_ > dma_enable_min_size_)
    {
      read_op_ = op;
      HAL_I2C_Master_Receive_DMA(i2c_handle_, slave_addr,
                                 reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                                 read_data.size_);
      read_buff_ = read_data;
      op.MarkAsRunning();
      if (op.type == ReadOperation::OperationType::BLOCK)
      {
        return op.data.sem->Wait(op.data.timeout);
      }
      return ErrorCode::OK;
    }
    else
    {
      auto ans = HAL_I2C_Master_Receive(i2c_handle_, slave_addr,
                                        reinterpret_cast<uint8_t *>(read_data.addr_),
                                        read_data.size_, HAL_MAX_DELAY) == HAL_OK
                     ? ErrorCode::OK
                     : ErrorCode::BUSY;
      op.UpdateStatus(false, std::forward<ErrorCode>(ans));
      return ans;
    }
  }

  ErrorCode Write(uint16_t slave_addr, ConstRawData write_data,
                  WriteOperation &op) override
  {
    if (i2c_handle_->State != HAL_I2C_STATE_READY)
    {
      return ErrorCode::BUSY;
    }

    memcpy(dma_buff_.addr_, write_data.addr_, write_data.size_);

    if (write_data.size_ > dma_enable_min_size_)
    {
      write_op_ = op;
      HAL_I2C_Master_Transmit_DMA(i2c_handle_, slave_addr,
                                  reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                                  write_data.size_);
      op.MarkAsRunning();
      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        return op.data.sem->Wait(op.data.timeout);
      }
      return ErrorCode::OK;
    }
    else
    {
      auto ans = HAL_I2C_Master_Transmit(i2c_handle_, slave_addr,
                                         reinterpret_cast<uint8_t *>(dma_buff_.addr_),
                                         write_data.size_, HAL_MAX_DELAY) == HAL_OK
                     ? ErrorCode::OK
                     : ErrorCode::BUSY;
      op.UpdateStatus(false, std::forward<ErrorCode>(ans));
      return ans;
    }
  }

  template <typename, typename = void>
  struct HasClockSpeed : std::false_type
  {
  };

  template <typename T>
  struct HasClockSpeed<T, std::void_t<decltype(std::declval<T>()->Init.ClockSpeed)>>
      : std::true_type
  {
  };

  template <typename T>
  typename std::enable_if<!HasClockSpeed<T>::value>::type SetClockSpeed(
      T &, const Configuration &)
  {
  }

  template <typename T>
  typename std::enable_if<HasClockSpeed<T>::value>::type SetClockSpeed(
      T &i2c_handle, const Configuration &config)
  {
    i2c_handle->Init.ClockSpeed = config.clock_speed;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    if (HasClockSpeed<decltype(i2c_handle_)>::value)
    {
      SetClockSpeed<decltype(i2c_handle_)>(i2c_handle_, config);
    }
    else
    {
      return ErrorCode::NOT_SUPPORT;
    }

    if (HAL_I2C_Init(i2c_handle_) != HAL_OK)
    {
      return ErrorCode::INIT_ERR;
    }
    return ErrorCode::OK;
  }

  stm32_i2c_id_t id_;
  I2C_HandleTypeDef *i2c_handle_;
  uint32_t dma_enable_min_size_;

  RawData dma_buff_;

  ReadOperation read_op_;
  WriteOperation write_op_;
  RawData read_buff_;

  static STM32I2C *map[STM32_I2C_NUMBER];  // NOLINT
};
}  // namespace LibXR

#endif
