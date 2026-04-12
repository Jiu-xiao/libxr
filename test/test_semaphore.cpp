#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <pthread.h>

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

}  // namespace

void test_semaphore()
{
  LibXR::Semaphore sem(0);
  LibXR::Thread thread;

  ASSERT(sem.Wait(0) == ErrorCode::TIMEOUT);

  sem.Post();
  sem.Post();
  ASSERT(sem.Wait(0) == ErrorCode::OK);
  ASSERT(sem.Wait(0) == ErrorCode::OK);
  ASSERT(sem.Wait(0) == ErrorCode::TIMEOUT);

  thread.Create<LibXR::Semaphore*>(
      &sem,
      [](LibXR::Semaphore* sem)
      {
        LibXR::Thread::Sleep(50);
        sem->Post();
        return;
      },
      "semaphore_thread", 512, LibXR::Thread::Priority::REALTIME);

  ASSERT(sem.Wait(200) == ErrorCode::OK);
  JoinThreadIfNeeded(thread);
}
