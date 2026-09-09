/**
 * @file async_wait.hpp
 * @brief 等待借用的 ASync 完成测试任务 / Wait for a borrowed ASync test job.
 *
 * 通过公开 GetStatus 取得完成结果后，调用方才可释放任务引用的局部数据。
 * Observe completion through GetStatus before releasing data referenced by the job.
 */
#pragma once

#include "async.hpp"
#include "test_assert.hpp"

namespace LibXR::Test::Detail
{
inline void WaitForJob(ASync& worker, uint32_t timeout_ms)
{
  TEST_ASSERT(timeout_ms > 0 && timeout_ms < UINT32_MAX / 2U);
  const uint32_t start = Thread::GetTime();
  for (;;)
  {
    const auto status = worker.GetStatus();
    if (status == ASync::Status::DONE)
    {
      return;
    }
    TEST_ASSERT(status == ASync::Status::BUSY);
    TEST_ASSERT(Thread::GetTime() - start < timeout_ms);
    Thread::Sleep(1);
  }
}
}  // namespace LibXR::Test::Detail
