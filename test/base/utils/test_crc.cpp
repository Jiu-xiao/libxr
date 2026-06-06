/**
 * @file test_crc.cpp
 * @brief CRC8 / CRC16 / CRC32 calculation and verification tests.
 *
 * Test items:
 * 1. Packed structure checksum generation: verify each CRC helper computes the trailer field over the intended prefix bytes.
 * 2. Checksum verification: verify the generated trailer makes the corresponding `Verify()` helper succeed.
 *
 * Test principle:
 * 1. Use packed payloads with trailing checksum fields, because this matches the dominant in-repo usage pattern for CRC helpers.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_crc()
{
  struct __attribute__((packed))
  {
    double a;
    char b;
    uint8_t crc;
  } test_crc8 = {.a = LibXR::PI, .b = 'X', .crc = 0};

  struct __attribute__((packed))
  {
    double a;
    char b;
    uint16_t crc;
  } test_crc16 = {.a = LibXR::PI * 2, .b = 'X', .crc = 0};

  struct __attribute__((packed))
  {
    double a;
    char b;
    uint32_t crc;
  } test_crc32 = {.a = LibXR::PI * 3, .b = 'X', .crc = 0};

  test_crc8.crc = LibXR::CRC8::Calculate(&test_crc8, sizeof(test_crc8) - sizeof(uint8_t));
  test_crc16.crc =
      LibXR::CRC16::Calculate(&test_crc16, sizeof(test_crc16) - sizeof(uint16_t));
  test_crc32.crc =
      LibXR::CRC32::Calculate(&test_crc32, sizeof(test_crc32) - sizeof(uint32_t));

  ASSERT(LibXR::CRC8::Verify(&test_crc8, sizeof(test_crc8)));
  ASSERT(LibXR::CRC16::Verify(&test_crc16, sizeof(test_crc16)));
  ASSERT(LibXR::CRC32::Verify(&test_crc32, sizeof(test_crc32)));
}
