#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_crc() {
  struct __attribute__((packed)) {
    double a;
    char b;
    uint8_t crc;
  } test_crc8 = {.a = M_PI, .b = 'X', .crc = 0};

  struct __attribute__((packed)) {
    double a;
    char b;
    uint16_t crc;
  } test_crc16 = {.a = M_PI * 2, .b = 'X', .crc = 0};

  struct __attribute__((packed)) {
    double a;
    char b;
    uint32_t crc;
  } test_crc32 = {.a = M_PI * 3, .b = 'X', .crc = 0};

  test_crc8.crc =
      LibXR::CRC8::Calculate(&test_crc8, sizeof(test_crc8) - sizeof(uint8_t));
  test_crc16.crc = LibXR::CRC16::Calculate(
      &test_crc16, sizeof(test_crc16) - sizeof(uint16_t));
  test_crc32.crc = LibXR::CRC32::Calculate(
      &test_crc32, sizeof(test_crc32) - sizeof(uint32_t));

  ASSERT(LibXR::CRC8::Verify(&test_crc8, sizeof(test_crc8)));
  ASSERT(LibXR::CRC16::Verify(&test_crc16, sizeof(test_crc16)));
  ASSERT(LibXR::CRC32::Verify(&test_crc32, sizeof(test_crc32)));
}