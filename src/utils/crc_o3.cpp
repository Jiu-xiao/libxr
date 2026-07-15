#include "crc.hpp"

uint8_t LibXR::CRC8::Calculate(const void* raw, size_t len)
{
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);
  if (!inited_)
  {
    GenerateTable();
  }

  uint8_t crc = INIT;
  while (len-- > 0)
  {
    crc = tab_[(crc ^ *buf++) & 0xff];
  }
  return crc;
}

uint16_t LibXR::CRC16::Calculate(const void* raw, size_t len)
{
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);
  if (!inited_)
  {
    GenerateTable();
  }

  uint16_t crc = INIT;
  while (len--)
  {
    crc = tab_[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
  }
  return crc;
}

uint32_t LibXR::CRC32::Calculate(const void* raw, size_t len)
{
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);
  if (!inited_)
  {
    GenerateTable();
  }

  uint32_t crc = INIT;
  while (len--)
  {
    crc = tab_[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
  }
  return crc;
}

uint64_t LibXR::CRC64::Calculate(const void* raw, size_t len)
{
  const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);
  if (!inited_)
  {
    GenerateTable();
  }

  uint64_t crc = INIT;
  while (len--)
  {
    crc = tab_[(crc ^ *buf++) & 0xff] ^ (crc >> 8U);
  }
  return crc;
}
