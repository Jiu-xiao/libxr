/**
 * @file test_thread.cpp
 * @brief runtime thread 创建与 sleep 原语测试。 Runtime thread creation and sleep
 * primitive tests.
 *
 * 测试项目 / Test items:
 * 1. 线程创建后的信号唤醒。 Thread creation/wakeup: verify a created runtime thread can
 * signal completion through a semaphore.
 * 2. 相对睡眠 `Sleep()`。 Relative sleep: verify `Thread::Sleep()` waits for
 * approximately the requested duration.
 * 3. 周期性 `SleepUntil()`。 Periodic sleep-until: verify `SleepUntil()` advances wakeups
 * monotonically across successive periods.
 *
 * 测试原理 / Test principles:
 * 1. 只通过 LibXR 时间 API 观察线程睡眠与周期唤醒语义，避免测试体依赖宿主平台调用。
 * Observe sleep and periodic wakeup semantics only through LibXR time APIs, so the test
 * body does not depend on host platform calls.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_thread`。 Test entry function `test_thread`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_thread()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Thread thread;
  LibXR::Semaphore sem(0);

  ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  ASSERT(sem.Wait(200) == LibXR::ErrorCode::OK);
  ASSERT(thread.Join() == LibXR::ErrorCode::OK);

  const auto sleep_start = LibXR::Timebase::GetMilliseconds();
  LibXR::Thread::Sleep(20);
  const auto sleep_elapsed = LibXR::Timebase::GetMilliseconds() - sleep_start;
  ASSERT(sleep_elapsed.ToMillisecond() >= 15);

  LibXR::ThreadTimestamp wakeup = LibXR::Thread::GetTime();
  const auto periodic_start = LibXR::Timebase::GetMilliseconds();
  LibXR::Thread::SleepUntil(wakeup, 10);
  const auto first_wakeup = LibXR::Timebase::GetMilliseconds();
  LibXR::Thread::SleepUntil(wakeup, 10);
  const auto second_wakeup = LibXR::Timebase::GetMilliseconds();
  ASSERT((first_wakeup - periodic_start).ToMillisecond() >= 8);
  ASSERT((second_wakeup - periodic_start).ToMillisecond() >= 18);
  ASSERT((second_wakeup - first_wakeup).ToMillisecond() < 1000);
}
