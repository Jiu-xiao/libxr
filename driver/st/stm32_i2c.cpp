#include "stm32_i2c.hpp"

#ifdef HAL_I2C_MODULE_ENABLED

using namespace LibXR;

STM32I2C *STM32I2C::map[STM32_I2C_NUMBER] = {nullptr};

stm32_i2c_id_t STM32_I2C_GetID(I2C_TypeDef *hi2c) {  // NOLINT
  if (hi2c == nullptr) {
    return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
  }
#ifdef I2C1
  else if (hi2c == I2C1) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C1;
  }
#endif
#ifdef I2C2
  else if (hi2c == I2C2) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C2;
  }
#endif
#ifdef I2C3
  else if (hi2c == I2C3) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C3;
  }
#endif
#ifdef I2C4
  else if (hi2c == I2C4) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C4;
  }
#endif
#ifdef I2C5
  else if (hi2c == I2C5) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C5;
  }
#endif
#ifdef I2C6
  else if (hi2c == I2C6) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C6;
  }
#endif
#ifdef I2C7
  else if (hi2c == I2C7) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C7;
  }
#endif
#ifdef I2C8
  else if (hi2c == I2C8) {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C8;
  }
#endif
  return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
}

extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  memcpy(i2c->read_buff_.addr_, i2c->dma_buff_.addr_, i2c->read_buff_.size_);
  i2c->read_op_.UpdateStatus(true, ErrorCode::OK);
}

extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
  STM32I2C *i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  i2c->write_op_.UpdateStatus(true, ErrorCode::OK);
}

#endif
