// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_i2c_def.hpp"

ch32_i2c_id_t ch32_i2c_get_id(I2C_TypeDef* addr)
{
  if (addr == nullptr)
  {
    return ch32_i2c_id_t::CH32_I2C_ID_ERROR;
  }
#if defined(I2C1)
  if (addr == I2C1)
  {
    return ch32_i2c_id_t::CH32_I2C1;
  }
#endif
#if defined(I2C2)
  if (addr == I2C2)
  {
    return ch32_i2c_id_t::CH32_I2C2;
  }
#endif
  return ch32_i2c_id_t::CH32_I2C_ID_ERROR;
}

I2C_TypeDef* ch32_i2c_get_instance_id(ch32_i2c_id_t id)
{
  switch (id)
  {
#if defined(I2C1)
    case CH32_I2C1:
    {
      return I2C1;
    }
#endif
#if defined(I2C2)
    case CH32_I2C2:
    {
      return I2C2;
    }
#endif
    default:
      return nullptr;
  }
}

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
