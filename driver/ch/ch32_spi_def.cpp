// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_spi_def.hpp"

ch32_spi_id_t ch32_spi_get_id(SPI_TypeDef* addr)
{
  if (addr == nullptr)
  {
    return ch32_spi_id_t::CH32_SPI_ID_ERROR;
  }
#if defined(SPI1)
  else if (addr == SPI1)
  {
    return ch32_spi_id_t::CH32_SPI1;
  }
#endif
#if defined(SPI2)
  else if (addr == SPI2)
  {
    return ch32_spi_id_t::CH32_SPI2;
  }
#endif
#if defined(SPI3)
  else if (addr == SPI3)
  {
    return ch32_spi_id_t::CH32_SPI3;
  }
#endif

  return ch32_spi_id_t::CH32_SPI_NUMBER;
}

SPI_TypeDef* ch32_spi_get_instance_id(ch32_spi_id_t id)
{
  switch (id)
  {
#if defined(SPI1)
    case CH32_SPI1:
      return SPI1;
#endif
#if defined(SPI2)
    case CH32_SPI2:
      return SPI2;
#endif
#if defined(SPI3)
    case CH32_SPI3:
      return SPI3;
#endif
    default:
      return nullptr;
  }
}

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
