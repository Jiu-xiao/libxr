/**
 * @file test_thread.cpp
 * @brief Thread 在 Linux 上的测试 / Thread tests on Linux.
 *
 * 检查线程创建、通知和 Join，以及 Sleep、SleepUntil 后经过的时间。
 * Check thread creation, notification, Join, and elapsed time after Sleep and SleepUntil.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_thread()
{
  LibXR::Thread thread;
  LibXR::Semaphore sem(0);

  TEST_ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  TEST_ASSERT(sem.Wait(200) == LibXR::ErrorCode::OK);
  TEST_ASSERT(thread.Join() == LibXR::ErrorCode::OK);

  const uint32_t sleep_start_ms = LibXR::Thread::GetTime();
  LibXR::Thread::Sleep(20);
  const uint32_t sleep_elapsed_ms = LibXR::Thread::GetTime() - sleep_start_ms;
  TEST_ASSERT(sleep_elapsed_ms >= 15);

  LibXR::MillisecondTimestamp wakeup = LibXR::Thread::GetTime();
  const uint32_t periodic_start_ms = wakeup;
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t first_wakeup_ms = LibXR::Thread::GetTime();
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t second_wakeup_ms = LibXR::Thread::GetTime();
  TEST_ASSERT(first_wakeup_ms - periodic_start_ms >= 8);
  TEST_ASSERT(second_wakeup_ms - periodic_start_ms >= 18);
  TEST_ASSERT(second_wakeup_ms >= first_wakeup_ms);
}
