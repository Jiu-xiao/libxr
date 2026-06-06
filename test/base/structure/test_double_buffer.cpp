/**
 * @file test_double_buffer.cpp
 * @brief `DoubleBuffer` pending/active half-switch tests.
 *
 * Test items:
 * 1. Initial split and pending state: verify the raw buffer is divided evenly and starts without pending data.
 * 2. Pending fill semantics: verify `FillPending()` stores bytes and rejects a second pending fill before switch.
 * 3. Switch semantics and bounds: verify active-half toggling and oversized pending writes are rejected.
 *
 * Test principle:
 * 1. Use one concrete backing buffer and inspect both addresses and lengths, because this utility is a pure storage-layout primitive.
 * 2. Check both accepted and rejected writes so the test documents the state machine, not just the happy path.
 */
#include "double_buffer.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "test.hpp"  // 提供 ASSERT 宏

void test_double_buffer()
{
  uint8_t buff[128] = {};  // 预分配大缓冲区
  LibXR::RawData raw(buff, sizeof(buff));

  LibXR::DoubleBuffer buffer(raw);

  ASSERT(buffer.Size() == 64);  // 被平分成两块

  // 1. 检查初始状态
  ASSERT(buffer.HasPending() == false);

  // 2. 写入 pending buffer
  uint8_t test_data[16];
  for (int i = 0; i < 16; ++i) test_data[i] = i;

  ASSERT(buffer.FillPending(test_data, 16) == true);
  ASSERT(buffer.HasPending() == true);
  ASSERT(buffer.GetPendingLength() == 16);
  ASSERT(std::memcmp(buffer.PendingBuffer(), test_data, 16) == 0);

  // 3. 禁止重复填充未发送的 buffer
  ASSERT(buffer.FillPending(test_data, 8) == false);

  // 4. 执行 Switch() 切换
  buffer.Switch();
  ASSERT(buffer.HasPending() == false);
  ASSERT(buffer.ActiveBuffer() == buff + 64);  // 被切换为另一半

  // 5. 再次填充
  for (int i = 0; i < 10; ++i) test_data[i] = i + 100;
  ASSERT(buffer.FillPending(test_data, 10) == true);
  ASSERT(std::memcmp(buffer.PendingBuffer(), test_data, 10) == 0);

  buffer.Switch();
  ASSERT(buffer.ActiveBuffer() == buff);  // 又回到了原来的 A 区

  // 6. 不合法长度填充
  ASSERT(buffer.FillPending(test_data, 80) == false);  // 超过单 buffer 长度
}
