/**
 * @file test_async.cpp
 * @brief ASync 任务状态测试 / ASync job-state tests.
 *
 * 检查两个提交入口的忙时拒绝、任务不被覆盖，以及完成状态只被一个观察者取走。
 * Check busy rejection through both entries, preserved jobs and single-consumer
 * completion acknowledgment.
 */

#include <barrier>
#include <cstddef>
#include <new>
#include <thread>

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
  auto rejected =
      LibXR::ASync::Job::Create([](bool, void*, LibXR::ASync*) { TEST_ASSERT(false); },
                                static_cast<void*>(nullptr));
  for (int i = 0; i < 10; i++)
  {
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::READY);
    TEST_ASSERT(context.job_count == i);
    const auto result = i % 2 == 0 ? async->AssignJob(async_cb)
                                   : async->AssignJobFromCallback(async_cb, false);
    TEST_ASSERT(result == LibXR::ErrorCode::OK);

    // 回调被信号量挡住，检查 BUSY 时不会因任务提前完成而失败。
    // The semaphore holds the callback, so BUSY cannot disappear before this check.
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::BUSY);
    TEST_ASSERT(async->AssignJob(rejected) == LibXR::ErrorCode::BUSY);
    TEST_ASSERT(async->AssignJobFromCallback(rejected, false) == LibXR::ErrorCode::BUSY);
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
    // DONE 仍占有原任务；取走完成状态前，两个入口都不能覆盖它。
    // DONE still owns the job; neither entry may replace it before acknowledgment.
    TEST_ASSERT(async->AssignJob(rejected) == LibXR::ErrorCode::BUSY);
    TEST_ASSERT(async->AssignJobFromCallback(rejected, false) == LibXR::ErrorCode::BUSY);
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::DONE);
    TEST_ASSERT(async->GetStatus() == LibXR::ASync::Status::READY);
  }

  TEST_ASSERT(async->AssignJob(async_cb) == LibXR::ErrorCode::OK);
  context.completion_gate.Post();
  const uint32_t start = LibXR::Thread::GetTime();
  while (async->status_.load(std::memory_order_acquire) != LibXR::ASync::Status::DONE)
  {
    TEST_ASSERT(LibXR::Thread::GetTime() - start < 1000U);
    LibXR::Thread::Yield();
  }
  // 同时取完成状态，只能有一个读取者得到 DONE，另一个得到 READY。
  // Concurrent observers must consume DONE once; the other observer sees READY.
  std::barrier ready(3);
  LibXR::ASync::Status observed[2]{};
  std::thread first(
      [&]
      {
        ready.arrive_and_wait();
        observed[0] = async->GetStatus();
      });
  std::thread second(
      [&]
      {
        ready.arrive_and_wait();
        observed[1] = async->GetStatus();
      });
  ready.arrive_and_wait();
  first.join();
  second.join();
  TEST_ASSERT((observed[0] == LibXR::ASync::Status::DONE &&
               observed[1] == LibXR::ASync::Status::READY) ||
              (observed[1] == LibXR::ASync::Status::DONE &&
               observed[0] == LibXR::ASync::Status::READY));
  TEST_ASSERT(context.job_count == 11);
}
