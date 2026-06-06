/**
 * @file test_thread.cpp
 * @brief runtime thread 创建与 sleep 原语测试。 Runtime thread creation and sleep primitive tests.
 *
 * 测试项目 / Test items:
 * 1. 线程创建后的信号唤醒。 Thread creation/wakeup: verify a created runtime thread can signal completion through a semaphore.
 * 2. 相对睡眠 `Sleep()`。 Relative sleep: verify `Thread::Sleep()` waits for approximately the requested duration.
 * 3. 周期性 `SleepUntil()`。 Periodic sleep-until: verify `SleepUntil()` advances wakeups monotonically across successive periods.
 *
 * 测试原理 / Test principles:
 * 1. 同时使用宿主单调时钟和 LibXR 时间戳，双重验证外部时长与内部周期唤醒表面。 Use monotonic host time alongside LibXR timestamps so the test checks both external elapsed time and LibXR's own periodic wakeup surface.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

#include <time.h>

namespace
{

/**
 * @brief 辅助函数 `JoinThreadIfNeeded`。 Helper function `JoinThreadIfNeeded`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

/**
 * @brief 辅助函数 `NowMonotonicMs`。 Helper function `NowMonotonicMs`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t NowMonotonicMs()
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

}  // namespace

/**
 * @brief 测试入口函数 `test_thread`。 Test entry function `test_thread`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
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
  JoinThreadIfNeeded(thread);

  const uint64_t sleep_start_ms = NowMonotonicMs();
  LibXR::Thread::Sleep(20);
  const uint64_t sleep_elapsed_ms = NowMonotonicMs() - sleep_start_ms;
  ASSERT(sleep_elapsed_ms >= 15);

  LibXR::MillisecondTimestamp wakeup = LibXR::Thread::GetTime();
  const uint32_t periodic_start_ms = wakeup;
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t first_wakeup_ms = LibXR::Thread::GetTime();
  LibXR::Thread::SleepUntil(wakeup, 10);
  const uint32_t second_wakeup_ms = LibXR::Thread::GetTime();
  ASSERT(first_wakeup_ms - periodic_start_ms >= 8);
  ASSERT(second_wakeup_ms - periodic_start_ms >= 18);
  ASSERT(second_wakeup_ms >= first_wakeup_ms);
}
