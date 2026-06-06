/**
 * @file test_async.cpp
 * @brief Runtime `ASync` worker lifecycle tests.
 *
 * Test items:
 * 1. Job assignment lifecycle: verify the worker transitions through `READY -> BUSY -> DONE -> READY` on repeated assignments.
 * 2. Job execution side effect: verify the bound job actually runs and mutates the target state once per assignment.
 *
 * Test principle:
 * 1. Observe the public status machine around a real worker thread, because runtime correctness here is primarily scheduling/lifecycle semantics.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_async()
{
  int async_arg = 0;
  auto async_cb = LibXR::ASync::Job::Create(
      [](bool in_isr, int* arg, LibXR::ASync* async)
      {
        UNUSED(async);
        UNUSED(in_isr);
        LibXR::Thread::Sleep(10);
        *arg = *arg + 1;
      },
      &async_arg);

  static LibXR::ASync async(512, LibXR::Thread::Priority::REALTIME);
  for (int i = 0; i < 10; i++)
  {
    ASSERT(async.GetStatus() == LibXR::ASync::Status::READY);
    async.AssignJob(async_cb);

    ASSERT(async_arg == i);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::BUSY);
    LibXR::Thread::Sleep(20);

    ASSERT(async_arg == i + 1);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::DONE);
    ASSERT(async.GetStatus() == LibXR::ASync::Status::READY);
  }

  pthread_cancel(async.thread_handle_);
}
