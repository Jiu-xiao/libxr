#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libxr_def.hpp"

namespace LibXR
{
namespace Detail
{
/**
 * @brief 在编译期生成反射形式 CRC 的 256 项查找表 /
 *        Generates a 256-entry reflected CRC lookup table at compile time
 * @tparam T 校验值类型（`uint8_t`、`uint16_t`、`uint32_t` 或 `uint64_t`） /
 *         Checksum type (`uint8_t`, `uint16_t`, `uint32_t` or `uint64_t`)
 * @tparam POLY 反射形式的多项式 / Reflected polynomial
 * @return 查找表 / The lookup table
 *
 * 查找表由 `constexpr` 求值得到，存放在只读数据段，没有运行期初始化，
 * 因此多线程或中断中首次调用 CRC 不存在竞争。
 * The table is produced by `constexpr` evaluation and lives in read-only data. There
 * is no runtime initialization, so the first CRC call from several threads or
 * interrupts cannot race.
 */
template <typename T, T POLY>
constexpr std::array<T, 256> GenerateReflectedCrcTable()
{
  std::array<T, 256> table{};
  for (size_t i = 0; i < table.size(); ++i)
  {
    T crc = static_cast<T>(i);
    for (int j = 0; j < 8; ++j)
    {
      crc = (crc & 1U) ? static_cast<T>((crc >> 1U) ^ POLY) : static_cast<T>(crc >> 1U);
    }
    table[i] = crc;
  }
  return table;
}
}  // namespace Detail

/**
 * @class CRC8
 * @brief 8 位循环冗余校验（CRC-8）计算类 / CRC-8 checksum computation class
 *
 * 该类实现了 CRC-8 校验算法，支持计算和验证数据的 CRC8 校验码。
 * This class implements the CRC-8 checksum algorithm, supporting computation and
 * verification.
 */
class CRC8
{
 private:
  static const uint8_t INIT = 0xFF;  ///< CRC8 初始值 / CRC8 initial value

 public:
  /// CRC8 查找表，编译期生成并存放在只读数据段 /
  /// CRC8 lookup table, generated at compile time and kept in read-only data
  static constexpr std::array<uint8_t, 256> tab_ =
      Detail::GenerateReflectedCrcTable<uint8_t, 0x8C>();

  CRC8() {}

  /**
   * @brief 计算数据的 CRC8 校验码 / Computes the CRC8 checksum for the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 计算得到的 CRC8 值 / Computed CRC8 value
   */
  static uint8_t Calculate(const void* raw, size_t len);

  /**
   * @brief 验证数据的 CRC8 校验码 / Verifies the CRC8 checksum of the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 校验成功返回 `true`，否则返回 `false` /
   *         Returns `true` if the checksum is valid, otherwise returns `false`
   */
  static bool Verify(const void* raw, size_t len)
  {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);

    if (len < 2)
    {
      return false;
    }
    uint8_t expected = Calculate(buf, len - sizeof(uint8_t));
    return expected == buf[len - sizeof(uint8_t)];
  }
};

/**
 * @class CRC16
 * @brief 16 位循环冗余校验（CRC-16）计算类 / CRC-16 checksum computation class
 *
 * 该类实现了 CRC-16 校验算法，支持计算和验证数据的 CRC16 校验码。
 * This class implements the CRC-16 checksum algorithm, supporting computation and
 * verification.
 */
class CRC16
{
 private:
  static const uint16_t INIT = 0xFFFF;  ///< CRC16 初始值 / CRC16 initial value

 public:
  /// CRC16 查找表，编译期生成并存放在只读数据段 /
  /// CRC16 lookup table, generated at compile time and kept in read-only data
  static constexpr std::array<uint16_t, 256> tab_ =
      Detail::GenerateReflectedCrcTable<uint16_t, 0x8408>();

  CRC16() {}

  /**
   * @brief 计算数据的 CRC16 校验码 / Computes the CRC16 checksum for the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 计算得到的 CRC16 值 / Computed CRC16 value
   */
  static uint16_t Calculate(const void* raw, size_t len);

  /**
   * @brief 验证数据的 CRC16 校验码 / Verifies the CRC16 checksum of the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 校验成功返回 `true`，否则返回 `false` /
   *         Returns `true` if the checksum is valid, otherwise returns `false`
   */
  static bool Verify(const void* raw, size_t len)
  {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);

    if (len < 2)
    {
      return false;
    }

    uint16_t actual = 0;
    std::memcpy(&actual, buf + len - sizeof(actual), sizeof(actual));

    uint16_t expected = Calculate(buf, len - sizeof(uint16_t));
    return expected == actual;
  }
};

/**
 * @class CRC32
 * @brief 32 位循环冗余校验（CRC-32）计算类 / CRC-32 checksum computation class
 *
 * 该类实现了 CRC-32 校验算法，支持计算和验证数据的 CRC32 校验码。
 * This class implements the CRC-32 checksum algorithm, supporting computation and
 * verification.
 */
class CRC32
{
 private:
  static const uint32_t INIT = 0xFFFFFFFF;  ///< CRC32 初始值 / CRC32 initial value

 public:
  /// CRC32 查找表，编译期生成并存放在只读数据段 /
  /// CRC32 lookup table, generated at compile time and kept in read-only data
  static constexpr std::array<uint32_t, 256> tab_ =
      Detail::GenerateReflectedCrcTable<uint32_t, 0xEDB88320U>();

  CRC32() {}

  /**
   * @brief 计算数据的 CRC32 校验码 / Computes the CRC32 checksum for the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 计算得到的 CRC32 值 / Computed CRC32 value
   */
  static uint32_t Calculate(const void* raw, size_t len);

  /**
   * @brief 验证数据的 CRC32 校验码 / Verifies the CRC32 checksum of the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 校验成功返回 `true`，否则返回 `false` /
   *         Returns `true` if the checksum is valid, otherwise returns `false`
   */
  static bool Verify(const void* raw, size_t len)
  {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(raw);

    if (len < sizeof(uint32_t))
    {
      return false;
    }

    uint32_t actual = 0;
    std::memcpy(&actual, buf + len - sizeof(actual), sizeof(actual));

    uint32_t expected = Calculate(buf, len - sizeof(uint32_t));
    return expected == actual;
  }
};

/**
 * @class CRC64
 * @brief 64 位循环冗余校验（CRC-64）计算类 / CRC-64 checksum computation class
 *
 * 该类实现了 CRC-64 校验算法，支持计算数据的 64 位校验值。
 * This class implements the CRC-64 checksum algorithm and supports computing
 * 64-bit checksums for input data.
 */
class CRC64
{
 private:
  static const uint64_t INIT =
      0xFFFFFFFFFFFFFFFFULL;  ///< CRC64 初始值 / CRC64 initial value

 public:
  /// CRC64 查找表，编译期生成并存放在只读数据段 /
  /// CRC64 lookup table, generated at compile time and kept in read-only data
  static constexpr std::array<uint64_t, 256> tab_ =
      Detail::GenerateReflectedCrcTable<uint64_t, 0xC96C5795D7870F42ULL>();

  CRC64() {}

  /**
   * @brief 计算数据的 CRC64 校验码 / Computes the CRC64 checksum for the given data
   * @param raw 输入数据指针 / Pointer to input data
   * @param len 数据长度 / Length of the data
   * @return 计算得到的 CRC64 值 / Computed CRC64 value
   */
  static uint64_t Calculate(const void* raw, size_t len);
};
}  // namespace LibXR
