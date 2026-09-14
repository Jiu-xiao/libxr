/**
 * @file test_double_buffer.cpp
 * @brief 检查双缓冲区初始化、填充和切换。 /
 * Tests double-buffer initialization, filling and switching.
 *
 * 检查空缓冲区、内存对齐，并拒绝重复填充和超长数据。
 * Checks empty buffers and alignment, rejecting repeated fills and oversized data.
 */

#include "double_buffer.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_double_buffer()
{
  alignas(size_t) uint8_t buff[128] = {};  // 预分配大缓冲区
  LibXR::RawData raw(buff, sizeof(buff));

  LibXR::DoubleBuffer init_later;
  init_later.Init(raw);
  TEST_ASSERT(init_later.Size() == 64);
  TEST_ASSERT(
      (reinterpret_cast<uintptr_t>(init_later.ActiveBuffer()) % alignof(size_t)) == 0U);
  TEST_ASSERT(
      (reinterpret_cast<uintptr_t>(init_later.PendingBuffer()) % alignof(size_t)) == 0U);

  LibXR::DoubleBuffer empty_buffer;
  LibXR::RawData empty_raw(nullptr, 0);
  empty_buffer.Init(empty_raw);
  TEST_ASSERT(empty_buffer.Size() == 0);
  TEST_ASSERT(empty_buffer.ActiveBuffer() == nullptr);
  TEST_ASSERT(empty_buffer.PendingBuffer() == nullptr);
  TEST_ASSERT(empty_buffer.HasPending() == false);
  TEST_ASSERT(empty_buffer.FillPending(nullptr, 0) == true);
  TEST_ASSERT(empty_buffer.HasPending() == true);
  TEST_ASSERT(empty_buffer.GetPendingLength() == 0);
  empty_buffer.Switch();
  TEST_ASSERT(empty_buffer.ActiveBuffer() == nullptr);
  TEST_ASSERT(empty_buffer.FillActive(nullptr, 0) == true);

  LibXR::DoubleBuffer buffer(raw);

  TEST_ASSERT(buffer.Size() == 64);  // 被平分成两块
  TEST_ASSERT((reinterpret_cast<uintptr_t>(buffer.ActiveBuffer()) % alignof(size_t)) ==
              0U);
  TEST_ASSERT((reinterpret_cast<uintptr_t>(buffer.PendingBuffer()) % alignof(size_t)) ==
              0U);

  // 1. 检查初始状态
  TEST_ASSERT(buffer.HasPending() == false);

  // 2. 写入 pending buffer
  uint8_t test_data[16];
  for (int i = 0; i < 16; ++i) test_data[i] = i;

  TEST_ASSERT(buffer.FillPending(test_data, 16) == true);
  TEST_ASSERT(buffer.HasPending() == true);
  TEST_ASSERT(buffer.GetPendingLength() == 16);
  TEST_ASSERT(std::memcmp(buffer.PendingBuffer(), test_data, 16) == 0);

  // 3. 禁止重复填充未发送的 buffer
  TEST_ASSERT(buffer.FillPending(test_data, 8) == false);

  // 4. 执行 Switch() 切换
  buffer.Switch();
  TEST_ASSERT(buffer.HasPending() == false);
  TEST_ASSERT(buffer.ActiveBuffer() == buff + 64);  // 被切换为另一半

  // 5. 再次填充
  for (int i = 0; i < 10; ++i) test_data[i] = i + 100;
  TEST_ASSERT(buffer.FillPending(test_data, 10) == true);
  TEST_ASSERT(std::memcmp(buffer.PendingBuffer(), test_data, 10) == 0);

  buffer.Switch();
  TEST_ASSERT(buffer.ActiveBuffer() == buff);  // 又回到了原来的 A 区

  buffer.FlipActiveBlock();
  TEST_ASSERT(buffer.ActiveBuffer() == buff + 64);

  // 6. 不合法长度填充
  TEST_ASSERT(buffer.FillPending(test_data, 80) == false);  // 超过单 buffer 长度
}
