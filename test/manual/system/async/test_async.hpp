/**
 * @file test_async.hpp
 * @brief 实际系统的 ASync 任务测试 / ASync job tests on the target system.
 *
 * 交替运行受控阻塞和快速任务，检查忙时拒绝、执行序号及重复复用。
 * Alternate gated and fast jobs to check busy rejection, execution sequence and reuse.
 */
#pragma once

#include <atomic>

#include "../async_wait.hpp"

namespace LibXR::Test
{
/**
 * @brief 检查任务状态和重复提交 / Check job states and repeated submissions.
 * @param worker 独占借用的 READY 状态 ASync / Exclusively borrowed READY ASync.
 * @param timeout_ms 每次协作等待的上限 / Deadline for each cooperating wait.
 * @param iterations 提交次数 / Number of submissions.
 */
inline void TestASync(ASync& worker, uint32_t timeout_ms = 1000,
                      uint32_t iterations = 1000)
{
  TEST_ASSERT(worker.GetStatus() == ASync::Status::READY);
  TEST_ASSERT(iterations > 0);
  struct Context
  {
    Semaphore release;
    Semaphore completed;
    std::atomic<uint32_t> calls{0};
    uint32_t timeout_ms;
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> gated{0};
  } context{{0}, {0}, 0, timeout_ms};
  auto job = ASync::Job::Create(
      [](bool, Context* context, ASync*)
      {
        const uint32_t sequence = context->sequence.load(std::memory_order_relaxed);
        if (context->gated.load(std::memory_order_relaxed) != 0)
        {
          TEST_ASSERT(context->release.Wait(context->timeout_ms) == ErrorCode::OK);
        }
        TEST_ASSERT(context->calls.load(std::memory_order_relaxed) == sequence - 1U);
        context->calls.store(sequence, std::memory_order_relaxed);
        context->completed.Post();
      },
      &context);
  auto rejected_job =
      ASync::Job::Create([](bool, Context*, ASync*) { TEST_ASSERT(false); }, &context);

  for (uint32_t iteration = 0; iteration < iterations; ++iteration)
  {
    TEST_ASSERT(worker.GetStatus() == ASync::Status::READY);
    const bool gated = iteration % 4U < 2U;
    context.sequence.store(iteration + 1U, std::memory_order_relaxed);
    context.gated.store(gated ? 1U : 0U, std::memory_order_relaxed);
    const auto result = iteration % 2U == 0 ? worker.AssignJob(job)
                                            : worker.AssignJobFromCallback(job, false);
    TEST_ASSERT(result == ErrorCode::OK);
    // 受控任务保证可以检查 BUSY；快速任务保留提交后立即完成的交错。
    // Gated jobs make BUSY checks deterministic; fast jobs exercise immediate completion.
    if (gated)
    {
      TEST_ASSERT(worker.GetStatus() == ASync::Status::BUSY);
      TEST_ASSERT(worker.AssignJob(rejected_job) == ErrorCode::BUSY);
      TEST_ASSERT(worker.AssignJobFromCallback(rejected_job, false) == ErrorCode::BUSY);
      context.release.Post();
    }
    TEST_ASSERT(context.completed.Wait(timeout_ms) == ErrorCode::OK);
    Detail::WaitForJob(worker, timeout_ms);
    TEST_ASSERT(context.calls.load(std::memory_order_relaxed) == iteration + 1U);
    TEST_ASSERT(context.completed.Wait(0) == ErrorCode::TIMEOUT);
    TEST_ASSERT(context.release.Wait(0) == ErrorCode::TIMEOUT);
    TEST_ASSERT(worker.GetStatus() == ASync::Status::READY);
  }
}
}  // namespace LibXR::Test
