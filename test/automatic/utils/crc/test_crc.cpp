/**
 * @file test_crc.cpp
 * @brief CRC 计算与校验测试 / CRC calculation and verification tests.
 *
 * 分别给打包数据附加 CRC8、CRC16 和 CRC32，再检查完整数据能通过校验。
 * Append CRC8, CRC16 and CRC32 to packed data and verify each complete record.
 */

#include "crc.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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

  TEST_ASSERT(LibXR::CRC8::Verify(&test_crc8, sizeof(test_crc8)));
  TEST_ASSERT(LibXR::CRC16::Verify(&test_crc16, sizeof(test_crc16)));
  TEST_ASSERT(LibXR::CRC32::Verify(&test_crc32, sizeof(test_crc32)));
}
