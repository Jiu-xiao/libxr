/**
 * @file test_async.cpp
 * @brief runtime `ASync` worker 生命周期测试。 Runtime `ASync` worker lifecycle tests.
 *
 * 测试项目 / Test items:
 * 1. job 分配的状态机。 Job assignment lifecycle: verify the worker transitions through `READY -> BUSY -> DONE -> READY` on repeated assignments.
 * 2. job 执行后的副作用。 Job execution side effect: verify the bound job actually runs and mutates the target state once per assignment.
 *
 * 测试原理 / Test principles:
 * 1. 围绕真实 worker thread 观察公开状态机，因为这里的正确性首先是调度和生命周期语义。 Observe the public status machine around a real worker thread, because runtime correctness here is primarily scheduling/lifecycle semantics.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_async()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
