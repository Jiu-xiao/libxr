#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_semaphore() {
  LibXR::Semaphore sem(0);
  LibXR::Thread thread;

  thread.Create<LibXR::Semaphore *>(
      &sem,
      [](LibXR::Semaphore *sem) {
        LibXR::Thread::Sleep(50);
        sem->Post();
        return;
      },
      "semaphore_thread", 512, LibXR::Thread::Priority::REALTIME);

  ASSERT(sem.Wait(100) == ErrorCode::OK);
}