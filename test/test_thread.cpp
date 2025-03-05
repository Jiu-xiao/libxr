#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_thread() {
  LibXR::Thread thread;

  LibXR::Semaphore sem(0);

  thread.Create<LibXR::Semaphore *>(
      &sem,
      [](LibXR::Semaphore *sem) {
        LibXR::Signal::Wait(5);
        sem->Post();
        return;
      },
      "test_task", 512, LibXR::Thread::Priority::REALTIME);

  LibXR::Thread::Sleep(100);

  LibXR::Signal::Action(thread, 5);

  ASSERT(sem.Wait(100) == ErrorCode::OK);

  pthread_join(thread, nullptr);
}