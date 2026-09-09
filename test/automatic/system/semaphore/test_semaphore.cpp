/**
 * @file test_semaphore.cpp
 * @brief Semaphore 计数与唤醒测试 / Semaphore counting and wakeup tests.
 *
 * 在 Linux 上检查空信号量超时、连续 Post 的计数，以及另一线程发出的通知。
 * Check empty waits, counted posts and notification from another thread on Linux.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_semaphore()
{
  LibXR::Semaphore sem(0);
  LibXR::Thread thread;

  TEST_ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  sem.Post();
  sem.Post();
  TEST_ASSERT(sem.Wait(0) == LibXR::ErrorCode::OK);
  TEST_ASSERT(sem.Wait(0) == LibXR::ErrorCode::OK);
  TEST_ASSERT(sem.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        LibXR::Thread::Sleep(50);
        sem->Post();
        return;
      },
      "semaphore_thread", 512, LibXR::Thread::Priority::REALTIME);

  TEST_ASSERT(sem.Wait(200) == LibXR::ErrorCode::OK);
  TEST_ASSERT(thread.Join() == LibXR::ErrorCode::OK);
}
