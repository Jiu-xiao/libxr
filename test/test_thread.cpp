#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_thread() {
  LibXR::Thread thread;

  static LibXR::Semaphore sem(0);

  ASSERT(sem.Wait(0) != ErrorCode::OK);

  thread.Create<LibXR::Semaphore *>(
      &sem,
      [](LibXR::Semaphore *sem) {
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(100);

  ASSERT(sem.Wait(100) == ErrorCode::OK);
}