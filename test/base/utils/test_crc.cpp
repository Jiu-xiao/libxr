/**
 * @file test_crc.cpp
 * @brief CRC8 / CRC16 / CRC32 计算与校验测试。 CRC8 / CRC16 / CRC32 calculation and verification tests.
 *
 * 测试项目 / Test items:
 * 1. 带尾校验字段的 packed 结构计算。 Packed structure checksum generation: verify each CRC helper computes the trailer field over the intended prefix bytes.
 * 2. 对应 `Verify()` 校验通过。 Checksum verification: verify the generated trailer makes the corresponding `Verify()` helper succeed.
 *
 * 测试原理 / Test principles:
 * 1. 使用末尾 CRC 字段的 packed 载荷，贴近仓库内最主要的真实用法。 Use packed payloads with trailing checksum fields, because this matches the dominant in-repo usage pattern for CRC helpers.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_crc`。 Test entry function `test_crc`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_crc()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
