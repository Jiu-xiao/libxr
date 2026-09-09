/**
 * @file test_mutex.cpp
 * @brief Mutex 加锁与竞争测试 / Mutex locking and contention tests.
 *
 * 在 Linux 上检查 TryLock 返回 BUSY、持锁时另一线程不能进入，以及解锁后继续执行。
 * Check TryLock BUSY, exclusion while locked, and waiter progress after unlock on Linux.
 */

#include <atomic>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
struct MutexAcquireContext
{
  LibXR::Mutex* mutex;
  std::atomic<bool>* acquired;
  LibXR::Semaphore* done;
};

void AcquireMutex(MutexAcquireContext* ctx)
{
  TEST_ASSERT(ctx->mutex->Lock() == LibXR::ErrorCode::OK);
  ctx->acquired->store(true, std::memory_order_release);
  ctx->mutex->Unlock();
  ctx->done->Post();
}

}  // namespace

void test_mutex()
{
  LibXR::Mutex mutex;
  LibXR::Semaphore done(0);
  std::atomic<bool> acquired(false);
  LibXR::Thread waiter;

  TEST_ASSERT(mutex.Lock() == LibXR::ErrorCode::OK);
  TEST_ASSERT(mutex.TryLock() == LibXR::ErrorCode::BUSY);

  MutexAcquireContext ctx = {&mutex, &acquired, &done};
  waiter.Create<MutexAcquireContext*>(&ctx, AcquireMutex, "mutex_waiter", 1024,
                                      LibXR::Thread::Priority::MEDIUM);

  LibXR::Thread::Sleep(20);
  TEST_ASSERT(!acquired.load(std::memory_order_acquire));

  mutex.Unlock();

  TEST_ASSERT(done.Wait(500) == LibXR::ErrorCode::OK);
  TEST_ASSERT(waiter.Join() == LibXR::ErrorCode::OK);
  TEST_ASSERT(acquired.load(std::memory_order_acquire));

  TEST_ASSERT(mutex.TryLock() == LibXR::ErrorCode::OK);
  mutex.Unlock();
}
