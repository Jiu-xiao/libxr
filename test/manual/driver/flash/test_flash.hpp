/**
 * @file test_flash.hpp
 * @brief 专用区域内的 Flash 擦写回读测试 / Flash erase, program and readback in a
 * reserved region.
 *
 * 整体擦除后按地址递增写入，再回读比较；重新擦除后写入反码，结束时再次擦除。
 * Erase the region, program in ascending order and compare reads; erase and repeat
 * with the inverted pattern, then erase once more. Check blank bytes only if specified.
 */
#pragma once

#include <cstring>
#include <optional>

#include "flash.hpp"
#include "test_assert.hpp"

namespace LibXR::Test
{
/**
 * @brief 检查合法区域内的顺序写入和擦除后重写 / Check sequential programming and
 * erase/rewrite.
 * @pre 区域必须专供测试、可整体擦除，内部擦写规则一致，不包含程序或待保留数据。
 *      Reserve a legally whole-erasable region with compatible programming rules,
 *      containing no firmware or data to preserve.
 * @pre buffer 是独占 RAM，大小为该区域合法写入批次，地址满足后端对 RAM 的对齐要求。
 *      Supply exclusive RAM with a legal write-batch size and backend-required RAM
 * alignment.
 * @param flash 已初始化的 Flash / Initialized Flash device.
 * @param offset 测试区相对 Flash 对象的起始偏移 / Region offset within the Flash object.
 * @param size 测试区大小，必须是 buffer 大小的整数倍 / Region size, a multiple of buffer
 * size.
 * @param buffer 写入和回读共用的工作缓冲区 / Shared programming/readback work buffer.
 * @param erased_value 该区域确定的擦除字节值，未知或未定义时传 nullopt /
 *        Defined erased byte for this region, or nullopt if unknown or unspecified.
 * @param iterations 重复轮数，总擦除次数为 2*iterations+1 / Rounds; 2*iterations+1 erases
 * total.
 */
inline void TestFlash(Flash& flash, size_t offset, size_t size, RawData buffer,
                      std::optional<uint8_t> erased_value, uint32_t iterations = 1)
{
  const size_t erase_size = flash.MinEraseSize();
  const size_t write_size = flash.MinWriteSize();
  TEST_ASSERT(erase_size > 0 && write_size > 0 && iterations > 0);
  TEST_ASSERT(offset <= flash.Size() && size > 0 && size <= flash.Size() - offset);
  TEST_ASSERT(offset % erase_size == 0 && size % erase_size == 0);
  TEST_ASSERT(offset % write_size == 0);
  TEST_ASSERT(buffer.addr_ != nullptr && buffer.size_ > 0 && buffer.size_ <= size);
  TEST_ASSERT(buffer.size_ % write_size == 0 && size % buffer.size_ == 0);
  auto* bytes = static_cast<uint8_t*>(buffer.addr_);

  auto erase = [&]
  {
    TEST_ASSERT(flash.Erase(offset, size) == ErrorCode::OK);
    if (erased_value.has_value())
    {
      for (size_t pos = 0; pos < size; pos += buffer.size_)
      {
        // 先填入错误值，避免 Read 未复制数据却误通过。
        // Prefill incorrect bytes so a Read that copies nothing cannot pass.
        std::memset(bytes, static_cast<uint8_t>(~*erased_value), buffer.size_);
        TEST_ASSERT(flash.Read(offset + pos, buffer) == ErrorCode::OK);
        for (size_t i = 0; i < buffer.size_; ++i)
        {
          TEST_ASSERT(bytes[i] == *erased_value);
        }
      }
    }
  };

  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (uint32_t inverted = 0; inverted < 2; ++inverted)
    {
      auto pattern = [&](size_t position)
      {
        // 251 字节循环不与常见的 2 的幂块大小重合；下一遍逐位取反。
        // A 251-byte cycle avoids common power-of-two block periods; invert next pass.
        const auto value = static_cast<uint8_t>(position % 251U + round);
        return static_cast<uint8_t>(value ^ (inverted ? 0xFFU : 0U));
      };
      erase();
      for (size_t pos = 0; pos < size; pos += buffer.size_)
      {
        for (size_t i = 0; i < buffer.size_; ++i)
        {
          bytes[i] = pattern(offset + pos + i);
        }
        TEST_ASSERT(flash.Write(offset + pos, {bytes, buffer.size_}) == ErrorCode::OK);
      }
      // 完成整遍写入后再读取，不穿插逆序写入或重复编程。
      // Read only after the full pass; never program backwards or program a unit twice.
      for (size_t pos = 0; pos < size; pos += buffer.size_)
      {
        for (size_t i = 0; i < buffer.size_; ++i)
        {
          bytes[i] = static_cast<uint8_t>(~pattern(offset + pos + i));
        }
        TEST_ASSERT(flash.Read(offset + pos, buffer) == ErrorCode::OK);
        for (size_t i = 0; i < buffer.size_; ++i)
        {
          TEST_ASSERT(bytes[i] == pattern(offset + pos + i));
        }
      }
    }
  }
  erase();
}
}  // namespace LibXR::Test
