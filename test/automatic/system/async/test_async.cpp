/**
 * @file test_async.cpp
 * @brief ASync 任务状态测试 / ASync job-state tests.
 *
 * 连续提交任务，检查 READY、BUSY、DONE 的变化及每次任务只执行一次。
 * Submit successive jobs and check READY/BUSY/DONE transitions and one execution per job.
 */

#include <cstddef>
#include <new>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_async()
{
  struct Context
  {
    LibXR::Semaphore completion_gate;
    int job_count = 0;
  };
  static Context context;
  auto async_cb = LibXR::ASync::Job::Create(
      [](bool in_isr, Context* context, LibXR::ASync* async)
      {
        UNUSED(async);
        UNUSED(in_isr);
        TEST_ASSERT(context->completion_gate.Wait(1000) == LibXR::ErrorCode::OK);
        ++context->job_count;
      },
      &context);

  // 工作线程常驻，使用静态存储并跳过 ASync 析构，避免退出时先销毁仍被线程使用的对象。
  // Keep static storage without an ASync destructor while its permanent worker can still
  // use it.
  alignas(LibXR::ASync) static std::byte async_storage[sizeof(LibXR::ASync)];
  static LibXR::ASync* async =
      new (async_storage) LibXR::ASync(512, LibXR::Thread::Priority::REALTIME);
  for (int i = 0; i < 10; i++)
  {
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::READY);
    TEST_ASSERT(context.job_count == i);
    TEST_ASSERT(async->AssignJob(async_cb) == LibXR::ErrorCode::OK);

    // 回调被信号量挡住，检查 BUSY 时不会因任务提前完成而失败。
    // The semaphore holds the callback, so BUSY cannot disappear before this check.
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::BUSY);
    context.completion_gate.Post();
    // GetStatus 会消费 DONE；先 acquire 观察完成，才能安全读取回调写入的普通计数。
    // GetStatus consumes DONE; first observe completion with acquire before reading the
    // plain counter.
    const uint32_t wait_start = LibXR::Thread::GetTime();
    while (async->status_.load(std::memory_order_acquire) != LibXR::ASync::Status::DONE &&
           LibXR::Thread::GetTime() - wait_start < 1000U)
    {
      LibXR::Thread::Yield();
    }

    TEST_ASSERT(async->status_.load(std::memory_order_acquire) ==
                LibXR::ASync::Status::DONE);
    TEST_ASSERT(context.job_count == i + 1);
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::DONE);
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::READY);
  }
}
