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
  ASSERT(buffer.PendingLength() == 16);
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
