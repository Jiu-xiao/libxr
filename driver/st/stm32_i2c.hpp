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
  STM32I2C(I2C_HandleTypeDef *hi2c, RawData dma_buff, uint32_t dma_enable_min_size = 3);

  ErrorCode Read(uint16_t slave_addr, RawData read_data, ReadOperation &op) override;

  ErrorCode Write(uint16_t slave_addr, ConstRawData write_data,
                  WriteOperation &op) override;

  ErrorCode MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                    ReadOperation &op, MemAddrLength mem_addr_size) override;

  ErrorCode MemWrite(uint16_t slave_addr, uint16_t mem_addr, ConstRawData write_data,
                     WriteOperation &op, MemAddrLength mem_addr_size) override;

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

  ErrorCode SetConfig(Configuration config) override;

  stm32_i2c_id_t id_;
  I2C_HandleTypeDef *i2c_handle_;
  uint32_t dma_enable_min_size_;

  RawData dma_buff_;

  ReadOperation read_op_;
  WriteOperation write_op_;
  RawData read_buff_;

  bool read_ = false;

  static STM32I2C *map[STM32_I2C_NUMBER];  // NOLINT
};
}  // namespace LibXR

#endif
