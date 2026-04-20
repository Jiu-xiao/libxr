#include <atomic>

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_POSIX_HOST)
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
  ASSERT(ctx->mutex->Lock() == LibXR::ErrorCode::OK);
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

  ASSERT(mutex.Lock() == LibXR::ErrorCode::OK);
  ASSERT(mutex.TryLock() == LibXR::ErrorCode::BUSY);

  MutexAcquireContext ctx = {&mutex, &acquired, &done};
  waiter.Create<MutexAcquireContext*>(&ctx, AcquireMutex, "mutex_waiter", 1024,
                                     LibXR::Thread::Priority::MEDIUM);

  LibXR::Thread::Sleep(20);
  ASSERT(!acquired.load(std::memory_order_acquire));

  mutex.Unlock();

  ASSERT(done.Wait(500) == LibXR::ErrorCode::OK);
  JoinThreadIfNeeded(waiter);
  ASSERT(acquired.load(std::memory_order_acquire));

  ASSERT(mutex.TryLock() == LibXR::ErrorCode::OK);
  mutex.Unlock();
}
