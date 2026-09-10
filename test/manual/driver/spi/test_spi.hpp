/**
 * @file test_spi.hpp
 * @brief SPI 全双工接线回环测试 / Wired full-duplex SPI loopback test.
 *
 * 在指定长度下反复同时收发，检查三种完成方式、接收内容和发送缓冲区保持不变。
 * Repeat equal-length transfers in three completion modes; check received bytes
 * and that the transmitted bytes remain unchanged. Requires MOSI wired to MISO.
 */
#pragma once

#include <initializer_list>

#include "spi.hpp"
#include "test_assert.hpp"
#include "transfer_wait.hpp"

namespace LibXR::Test
{
/**
 * @brief 检查不同长度下的全双工回环 / Check full-duplex loopback at selected lengths.
 * @pre 初始化为 8 位帧全双工主机，MOSI 接 MISO，MISO 不连接其他输出。
 *      Configure an 8-bit full-duplex master; wire MOSI to MISO without other outputs on
 * MISO.
 * @pre 独占总线；tx/rx 是独立工作 RAM，不能与彼此或后端正在使用的缓冲区重叠。
 *      Reserve the bus; tx/rx must be separate work RAM, disjoint from each other and
 * active backend buffers.
 * @param spi 已初始化的 SPI / Initialized SPI controller.
 * @param tx 发送工作缓冲区，内容会被覆盖 / Transmit work buffer; contents are
 * overwritten.
 * @param rx 接收工作缓冲区，内容会被覆盖 / Receive work buffer; contents are overwritten.
 * @param lengths 每轮测试的字节数列表，须满足后端容量和对齐要求 /
 *        Byte lengths tested each round, within backend capacity/alignment requirements.
 * @param timeout_ms BLOCK 超时及返回后的完成等待上限 / BLOCK timeout and post-return wait
 * limit.
 * @param iterations 重复轮数，每个长度均测试三种完成方式 /
 *        Rounds; every length uses all three completion modes.
 */
inline void TestSPILoopback(SPI& spi, RawData tx, RawData rx,
                            std::initializer_list<size_t> lengths,
                            uint32_t timeout_ms = 1000, uint32_t iterations = 1000)
{
  TEST_ASSERT(tx.addr_ != nullptr && rx.addr_ != nullptr);
  TEST_ASSERT(iterations > 0 && lengths.size() > 0 &&
              lengths.size() <= UINT32_MAX / iterations);
  const auto tx_begin = reinterpret_cast<uintptr_t>(tx.addr_);
  const auto rx_begin = reinterpret_cast<uintptr_t>(rx.addr_);
  TEST_ASSERT(tx_begin >= rx_begin ? tx_begin - rx_begin >= rx.size_
                                   : rx_begin - tx_begin >= tx.size_);
  for (size_t length : lengths)
  {
    TEST_ASSERT(length > 0 && length <= tx.size_ && length <= rx.size_);
  }
  auto* sent = static_cast<uint8_t*>(tx.addr_);
  auto* received = static_cast<uint8_t*>(rx.addr_);
  Detail::TransferTestCompletion completion(timeout_ms);
  for (uint32_t round = 0; round < iterations; ++round)
  {
    for (size_t length : lengths)
    {
      for (unsigned mode = 0; mode < 3; ++mode)
      {
        const auto seed = static_cast<uint8_t>(round + length + mode * 85U);
        auto expected = [&](size_t i) { return static_cast<uint8_t>((i % 251U) ^ seed); };
        for (size_t i = 0; i < length; ++i)
        {
          sent[i] = expected(i);
          received[i] = static_cast<uint8_t>(~expected(i));
        }
        completion.Run(mode,
                       [&](SPI::OperationRW& operation)
                       {
                         return spi.ReadAndWrite({received, length}, {sent, length},
                                                 operation, false);
                       });
        // 分别对照生成规则，避免收发缓冲区被同时改坏后仍相等。
        // Check both against the pattern, not just each other, to catch shared
        // corruption.
        for (size_t i = 0; i < length; ++i)
        {
          TEST_ASSERT(sent[i] == expected(i));
          TEST_ASSERT(received[i] == expected(i));
        }
        completion.Check();
      }
    }
  }
}
}  // namespace LibXR::Test
