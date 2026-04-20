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
#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

}  // namespace

void test_semaphore()
{
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
