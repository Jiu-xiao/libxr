#include <atomic>
#include <pthread.h>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_Linux) || defined(LIBXR_SYSTEM_Webots)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

struct MutexAcquireContext
{
  LibXR::Mutex* mutex;
  std::atomic<bool>* acquired;
  LibXR::Semaphore* done;
};

void AcquireMutex(MutexAcquireContext* ctx)
{
  ASSERT(ctx->mutex->Lock() == ErrorCode::OK);
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

  ASSERT(mutex.Lock() == ErrorCode::OK);
  ASSERT(mutex.TryLock() == ErrorCode::BUSY);

  MutexAcquireContext ctx = {&mutex, &acquired, &done};
  waiter.Create<MutexAcquireContext*>(&ctx, AcquireMutex, "mutex_waiter", 1024,
                                     LibXR::Thread::Priority::MEDIUM);

  LibXR::Thread::Sleep(20);
  ASSERT(!acquired.load(std::memory_order_acquire));

  mutex.Unlock();

  ASSERT(done.Wait(500) == ErrorCode::OK);
  JoinThreadIfNeeded(waiter);
  ASSERT(acquired.load(std::memory_order_acquire));

  ASSERT(mutex.TryLock() == ErrorCode::OK);
  mutex.Unlock();
}
