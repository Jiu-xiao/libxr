/**
 * @file test_i2c.hpp
 * @brief I2C 寄存器读写测试 / I2C register read and write tests.
 *
 * 用阻塞、轮询和回调方式检查固定寄存器，以及两组调用方数据的写入回读。
 * Check fixed registers and write/readback of two caller-supplied patterns in
 * blocking, polling and callback modes. Device-specific values belong to the caller.
 */
#pragma once

#include <cstring>

#include "i2c.hpp"
#include "test_assert.hpp"
#include "thread.hpp"
#include "transfer_wait.hpp"

namespace LibXR::Test
{
namespace Detail
{
inline void CheckI2CRegister(uint16_t slave_addr, uint16_t mem_addr,
                             I2C::MemAddrLength addr_length)
{
  TEST_ASSERT(slave_addr <= 0x3FFU);
  TEST_ASSERT(addr_length == I2C::MemAddrLength::BYTE_8 ||
              addr_length == I2C::MemAddrLength::BYTE_16);
  TEST_ASSERT(addr_length != I2C::MemAddrLength::BYTE_8 || mem_addr <= 0xFFU);
}

inline void CheckI2CBuffers(ConstRawData expected, RawData buffer)
{
  TEST_ASSERT(expected.addr_ != nullptr && buffer.addr_ != nullptr);
  TEST_ASSERT(expected.size_ > 0 && expected.size_ <= buffer.size_);
  const auto source = reinterpret_cast<uintptr_t>(expected.addr_);
  const auto destination = reinterpret_cast<uintptr_t>(buffer.addr_);
  TEST_ASSERT(source >= destination ? source - destination >= expected.size_
                                    : destination - source >= expected.size_);
}

inline void PoisonI2CBuffer(ConstRawData expected, RawData buffer)
{
  const auto* wanted = static_cast<const uint8_t*>(expected.addr_);
  auto* received = static_cast<uint8_t*>(buffer.addr_);
  for (size_t i = 0; i < expected.size_; ++i)
  {
    received[i] = static_cast<uint8_t>(~wanted[i]);
  }
}

inline void CheckI2CData(ConstRawData expected, RawData buffer)
{
  const auto* wanted = static_cast<const uint8_t*>(expected.addr_);
  const auto* received = static_cast<const uint8_t*>(buffer.addr_);
  for (size_t i = 0; i < expected.size_; ++i)
  {
    TEST_ASSERT(received[i] == wanted[i]);
  }
}

}  // namespace Detail

/**
 * @brief 检查三种完成方式下的固定寄存器读数 / Check fixed reads in three completion
 * modes.
 * @pre 总线和设备已初始化并由测试独占，寄存器允许重复读取且内容固定。
 *      Initialize and reserve the bus/device; registers must support stable repeated
 * reads.
 * @param i2c 已初始化的 I2C / Initialized I2C controller.
 * @param slave_addr 不带 R/W 位的从机地址 / Slave address without the R/W bit.
 * @param mem_addr 起始寄存器地址 / Starting register address.
 * @param addr_length 寄存器地址长度 / Register address width.
 * @param expected 预期字节，决定读取长度，不能与接收区重叠 / Expected bytes defining the
 *        read length; must not overlap the receive area.
 * @param buffer 接收区，容量至少为 expected 大小 / Receive storage, at least expected
 * size.
 * @param timeout_ms BLOCK 超时及返回后的完成等待上限 / BLOCK timeout and post-return wait
 * limit.
 * @param iterations 轮数，每轮三次读取 / Rounds, with three reads per round.
 */
inline void TestI2CMemRead(I2C& i2c, uint16_t slave_addr, uint16_t mem_addr,
                           I2C::MemAddrLength addr_length, ConstRawData expected,
                           RawData buffer, uint32_t timeout_ms = 1000,
                           uint32_t iterations = 1000)
{
  Detail::CheckI2CRegister(slave_addr, mem_addr, addr_length);
  Detail::CheckI2CBuffers(expected, buffer);
  TEST_ASSERT(iterations > 0);
  Detail::TransferTestCompletion completion(timeout_ms);
  const RawData read_buffer{buffer.addr_, expected.size_};
  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (unsigned mode = 0; mode < 3; ++mode)
    {
      Detail::PoisonI2CBuffer(expected, read_buffer);
      completion.Run(mode,
                     [&](ReadOperation& operation)
                     {
                       return i2c.MemRead(slave_addr, mem_addr, read_buffer, operation,
                                          addr_length, false);
                     });
      Detail::CheckI2CData(expected, read_buffer);
      completion.Check();
    }
  }
}

/**
 * @brief 交替写入两组合法数据并回读 / Alternate two legal patterns and check readback.
 * @pre 目标区域须专供测试，支持原样回读；两组数据均合法且长度相同。
 *      Reserve a region that reads back written bytes; both patterns must be legal and
 * equal-sized.
 * @pre 调用方负责分页、写入寿命、保存与恢复原值；成功后保留第二组数据。
 *      The caller handles paging, endurance and saving/restoring old values. Leaves the
 * second pattern.
 * @param i2c 已初始化并独占的总线 / Initialized, exclusively reserved bus.
 * @param slave_addr 不带 R/W 位的从机地址 / Slave address without the R/W bit.
 * @param mem_addr 可读写区域起始地址 / Start of the read/write region.
 * @param addr_length 寄存器地址长度 / Register address width.
 * @param first 第一组合法写入数据 / First legal write pattern.
 * @param second 不同的第二组数据 / Distinct second pattern.
 * @param buffer 独立回读缓冲区，不能与任一写入数据重叠 / Separate receive buffer,
 * disjoint from both patterns.
 * @param settle_ms 写完成到回读之间的等待时间 / Delay between write completion and
 * readback.
 * @param timeout_ms BLOCK 超时及返回后的完成等待上限 / BLOCK timeout and post-return wait
 * limit.
 * @param iterations 轮数，每轮各六次写入和读取 / Rounds, each with six writes and six
 * reads.
 */
inline void TestI2CMemWriteRead(I2C& i2c, uint16_t slave_addr, uint16_t mem_addr,
                                I2C::MemAddrLength addr_length, ConstRawData first,
                                ConstRawData second, RawData buffer,
                                uint32_t settle_ms = 0, uint32_t timeout_ms = 1000,
                                uint32_t iterations = 100)
{
  Detail::CheckI2CRegister(slave_addr, mem_addr, addr_length);
  Detail::CheckI2CBuffers(first, buffer);
  Detail::CheckI2CBuffers(second, buffer);
  TEST_ASSERT(first.size_ == second.size_);
  TEST_ASSERT(std::memcmp(first.addr_, second.addr_, first.size_) != 0);
  TEST_ASSERT(settle_ms < UINT32_MAX / 2U && iterations > 0 &&
              iterations <= UINT32_MAX / 4U);
  Detail::TransferTestCompletion completion(timeout_ms);
  const ConstRawData patterns[] = {first, second};
  const RawData read_buffer{buffer.addr_, first.size_};
  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (unsigned mode = 0; mode < 3; ++mode)
    {
      for (const auto& pattern : patterns)
      {
        completion.Run(mode,
                       [&](WriteOperation& operation)
                       {
                         return i2c.MemWrite(slave_addr, mem_addr, pattern, operation,
                                             addr_length, false);
                       });
        Thread::Sleep(settle_ms);
        Detail::PoisonI2CBuffer(pattern, read_buffer);
        completion.Run(mode,
                       [&](ReadOperation& operation)
                       {
                         return i2c.MemRead(slave_addr, mem_addr, read_buffer, operation,
                                            addr_length, false);
                       });
        Detail::CheckI2CData(pattern, read_buffer);
        completion.Check();
      }
    }
  }
}
}  // namespace LibXR::Test
