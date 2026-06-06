/**
 * @file test_semaphore.cpp
 * @brief runtime semaphore post/wait 测试。 Runtime semaphore post/wait tests.
 *
 * 测试项目 / Test items:
 * 1. 空信号量立即超时与预先 post 的缓存令牌。 Immediate timeout and queued posts: verify zero-time waits time out on empty state and consume pre-posted counts correctly.
 * 2. 延迟 post 对阻塞 wait 的唤醒。 Delayed post wakeup: verify a runtime thread can wake a blocking wait by posting later.
 *
 * 测试原理 / Test principles:
 * 1. 同时检查缓存计数和运行时唤醒，因为 semaphore 契约同时覆盖两种模式。 Check both preloaded counts and delayed runtime wakeup, because the semaphore contract spans cached tokens and blocking synchronization.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

namespace
{

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

}  // namespace

void test_semaphore()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::Semaphore sem(0);
  LibXR::Thread thread;

  ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  sem.Post();
  sem.Post();
  ASSERT(sem.Wait(0) == LibXR::ErrorCode::OK);
  ASSERT(sem.Wait(0) == LibXR::ErrorCode::OK);
  ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        LibXR::Thread::Sleep(50);
        sem->Post();
        return;
      },
      "semaphore_thread", 512, LibXR::Thread::Priority::REALTIME);

  ASSERT(sem.Wait(200) == LibXR::ErrorCode::OK);
  JoinThreadIfNeeded(thread);
}
