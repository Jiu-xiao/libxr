/**
 * @file test_semaphore.cpp
 * @brief runtime semaphore post/wait 测试。 Runtime semaphore post/wait tests.
 *
 * 测试项目 / Test items:
 * 1. 空信号量立即超时与预先 post 的缓存令牌。 Immediate timeout and queued posts: verify
 * zero-time waits time out on empty state and consume pre-posted counts correctly.
 * 2. 延迟 post 对阻塞 wait 的唤醒。 Delayed post wakeup: verify a runtime thread can wake
 * a blocking wait by posting later.
 * 3. Multiple-waiter wakeup: verify two posts release two already-blocked waiters without
 * leaving a cached count behind.
 *
 * 测试原理 / Test principles:
 * 1. 同时检查缓存计数和运行时唤醒，因为 semaphore 契约同时覆盖两种模式。 Check both
 * preloaded counts and delayed runtime wakeup, because the semaphore contract spans
 * cached tokens and blocking synchronization.
 */
#include <sched.h>

#include "../../common/system/linux_futex_wait_test_common.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
struct SemaphoreWaiterContext
{
  LibXR::Semaphore* semaphore;
  LibXR::Semaphore* done;
  std::atomic<pid_t>* thread_id;
};

void WaitOnSemaphore(SemaphoreWaiterContext* context)
{
  context->thread_id->store(LibXRTest::CurrentLinuxThreadId(), std::memory_order_release);
  ASSERT(context->semaphore->Wait(UINT32_MAX) == LibXR::ErrorCode::OK);
  context->done->Post();
}
}  // namespace

/**
 * @brief 测试入口函数 `test_semaphore`。 Test entry function `test_semaphore`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
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
  ASSERT(thread.Join() == LibXR::ErrorCode::OK);

  cpu_set_t original_affinity{};
  ASSERT(sched_getaffinity(0, sizeof(original_affinity), &original_affinity) == 0);
  ASSERT(sched_getscheduler(0) != SCHED_IDLE);

  int test_cpu = -1;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
  {
    if (CPU_ISSET(cpu, &original_affinity))
    {
      test_cpu = cpu;
      break;
    }
  }
  ASSERT(test_cpu >= 0);

  cpu_set_t single_cpu_affinity{};
  CPU_ZERO(&single_cpu_affinity);
  CPU_SET(test_cpu, &single_cpu_affinity);
  ASSERT(sched_setaffinity(0, sizeof(single_cpu_affinity), &single_cpu_affinity) == 0);

  LibXR::Semaphore shared(0);
  LibXR::Semaphore done(0);
  LibXR::Thread first_waiter;
  LibXR::Thread second_waiter;
  std::atomic<pid_t> first_thread_id{0};
  std::atomic<pid_t> second_thread_id{0};
  SemaphoreWaiterContext first_context{&shared, &done, &first_thread_id};
  SemaphoreWaiterContext second_context{&shared, &done, &second_thread_id};

  first_waiter.Create<SemaphoreWaiterContext*>(&first_context, WaitOnSemaphore,
                                               "semaphore_waiter_1", 512,
                                               LibXR::Thread::Priority::REALTIME);
  second_waiter.Create<SemaphoreWaiterContext*>(&second_context, WaitOnSemaphore,
                                                "semaphore_waiter_2", 512,
                                                LibXR::Thread::Priority::REALTIME);

  ASSERT(LibXRTest::WaitForLinuxFutexWait(first_thread_id));
  ASSERT(LibXRTest::WaitForLinuxFutexWait(second_thread_id));

  sched_param idle_priority{};
  ASSERT(sched_setscheduler(first_thread_id.load(std::memory_order_acquire), SCHED_IDLE,
                            &idle_priority) == 0);
  ASSERT(sched_setscheduler(second_thread_id.load(std::memory_order_acquire), SCHED_IDLE,
                            &idle_priority) == 0);

  // Both waiters inherit this thread's single-CPU affinity and are now SCHED_IDLE. They
  // cannot consume the first token while this non-SCHED_IDLE thread performs the second
  // Post.
  shared.Post();
  shared.Post();

  ASSERT(done.Wait(500) == LibXR::ErrorCode::OK);
  ASSERT(done.Wait(500) == LibXR::ErrorCode::OK);
  ASSERT(first_waiter.Join() == LibXR::ErrorCode::OK);
  ASSERT(second_waiter.Join() == LibXR::ErrorCode::OK);
  ASSERT(shared.Value() == 0U);
  ASSERT(sched_setaffinity(0, sizeof(original_affinity), &original_affinity) == 0);
}
