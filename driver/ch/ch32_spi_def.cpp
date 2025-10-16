#include "ch32_spi_def.hpp"

ch32_spi_id_t CH32_SPI_GetID(SPI_TypeDef *addr)
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

SPI_TypeDef *CH32_SPI_GetInstanceID(ch32_spi_id_t id)
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
