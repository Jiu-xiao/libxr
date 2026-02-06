#include "ch32_i2c_def.hpp"

ch32_i2c_id_t CH32_I2C_GetID(I2C_TypeDef *addr)
{
  if (addr == nullptr) return ch32_i2c_id_t::CH32_I2C_ID_ERROR;
#if defined(I2C1)
  if (addr == I2C1) return ch32_i2c_id_t::CH32_I2C1;
#endif
#if defined(I2C2)
  if (addr == I2C2) return ch32_i2c_id_t::CH32_I2C2;
#endif
  return ch32_i2c_id_t::CH32_I2C_ID_ERROR;
}

I2C_TypeDef *CH32_I2C_GetInstanceID(ch32_i2c_id_t id)
{
  switch (id)
  {
#if defined(I2C1)
    case CH32_I2C1:
      return I2C1;
#endif
#if defined(I2C2)
    case CH32_I2C2:
      return I2C2;
#endif
    default:
      return nullptr;
  }
}
