/**
 * @file rw_thread_test_common.hpp
 * @brief `rw` / `pipe` 线程与等待同步测试 helper。 Shared thread and waiter helpers for
 * `rw` / `pipe` tests.
 * @details 测试项目：
 *          1. 提供线程收尾与等待断言 helper。
 *          2. 提供挂起读写完成、阻塞读写调用的上下文与线程启动封装。
 *          3. 提供把后台补数据/补完成的动作统一包装成可复用 helper。
 *          Test items:
 *          1. Provide thread-settle and wait assertion helpers.
 *          2. Provide contexts and starters for pending completion and blocking
 * read/write calls.
 *          3. Wrap background completion actions into reusable helpers.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "../system/linux_futex_wait_test_common.hpp"
#include "rw_mode_test_common.hpp"

namespace
{
using LibXRTest::ALL_MODES;
using LibXRTest::ASYNC_MODES;
using LibXRTest::ASYNC_TIMEOUT_MS;
using LibXRTest::BLOCK_OPERATION_TIMEOUT_MS;
using LibXRTest::CurrentLinuxThreadId;
using LibXRTest::LinuxFutexWaitMode;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::THREAD_STATE_TIMEOUT_MS;
using LibXRTest::WaitForLinuxFutexWait;
using LibXRTest::WaitForLinuxFutexWaitMode;
using LibXRTest::WriteHarness;

/**
 * @brief 辅助函数 `JoinThreadIfNeeded`。 Helper function `JoinThreadIfNeeded`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  REQUIRE(thread.Join() == LibXR::ErrorCode::OK);
}

/**
 * @brief 断言辅助函数 `ExpectWaitOk`。 Assertion helper function `ExpectWaitOk`.
 * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check
 * to the current result. 测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。
 * Concentrate repeated validation logic so test items do not drift to inconsistent
 * checks.
 */
inline void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = ASYNC_TIMEOUT_MS)
{
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
  REQUIRE(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

struct ReadQueueCompletionContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  const uint8_t* data;
  size_t size;
  pid_t target_thread_id;
  std::atomic<bool>* target_armed;
};

/**
 * @brief 辅助函数 `CompletePendingReadFromQueue`。 Helper function
 * `CompletePendingReadFromQueue`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void CompletePendingReadFromQueue(ReadQueueCompletionContext ctx)
{
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(THREAD_STATE_TIMEOUT_MS);
  while (!ctx.target_armed->load(std::memory_order_acquire))
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    LibXR::Thread::Yield();
  }
  REQUIRE(WaitForLinuxFutexWait(ctx.target_thread_id));

  auto ans = ctx.port->queue_data_->PushBatch(ctx.data, ctx.size);
  UNUSED(ans);
  ASSERT(ans == LibXR::ErrorCode::OK);
  ctx.port->ProcessPendingReads(false);
  ctx.done->Post();
}

struct WriteFinishContext
{
  LibXR::WritePort* port;
  LibXR::Semaphore* done;
  LibXR::ErrorCode result;
};

/**
 * @brief 辅助函数 `FinishPendingWrite`。 Helper function `FinishPendingWrite`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void FinishPendingWrite(WriteFinishContext ctx)
{
  LibXR::WriteInfoBlock completed{};
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(THREAD_STATE_TIMEOUT_MS);

  while (ctx.port->QueueInfo()->Peek(completed) != LibXR::ErrorCode::OK)
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    LibXR::Thread::Yield();
  }

  {
    auto dequeue = ctx.port->BeginDequeue(false);
    REQUIRE(dequeue.PopInfo(completed) == LibXR::ErrorCode::OK);
    REQUIRE(dequeue.DiscardData(completed.data.size_) == LibXR::ErrorCode::OK);
  }

  ctx.port->Finish(false, ctx.result, completed);
  ctx.done->Post();
}

struct BlockingReadCallContext
{
  LibXR::ReadPort* port;
  LibXR::RawData data;
  uint32_t timeout_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
  std::atomic<pid_t> thread_id{0};
  LibXR::Semaphore* semaphore = nullptr;
};

/**
 * @brief 辅助函数 `BlockingReadCall`。 Helper function `BlockingReadCall`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void BlockingReadCall(BlockingReadCallContext* ctx)
{
  LibXR::Semaphore local_semaphore(0);
  LibXR::Semaphore& semaphore =
      ctx->semaphore == nullptr ? local_semaphore : *ctx->semaphore;
  LibXR::ReadOperation op(semaphore, ctx->timeout_ms);
  ctx->thread_id.store(CurrentLinuxThreadId(), std::memory_order_release);
  ctx->result = (*ctx->port)(ctx->data, op);
  ctx->done->Post();
}

struct BlockingWriteCallContext
{
  LibXR::WritePort* port;
  LibXR::ConstRawData data;
  uint32_t timeout_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
  std::atomic<pid_t> thread_id{0};
  LibXR::Semaphore* semaphore = nullptr;
};

/**
 * @brief 辅助函数 `BlockingWriteCall`。 Helper function `BlockingWriteCall`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform,
 * measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate
 * repeated helper logic locally so the main test body stays focused on the test item
 * itself.
 */
void BlockingWriteCall(BlockingWriteCallContext* ctx)
{
  LibXR::Semaphore local_semaphore(0);
  LibXR::Semaphore& semaphore =
      ctx->semaphore == nullptr ? local_semaphore : *ctx->semaphore;
  LibXR::WriteOperation op(semaphore, ctx->timeout_ms);
  ctx->thread_id.store(CurrentLinuxThreadId(), std::memory_order_release);
  ctx->result = (*ctx->port)(ctx->data, op);
  ctx->done->Post();
}

void StartReadQueueCompleter(LibXR::Thread& thread, LibXR::ReadPort& port,
                             LibXR::Semaphore& done, std::atomic<bool>& target_armed,
                             const uint8_t* data, size_t size, const char* name)
{
  thread.Create(ReadQueueCompletionContext{&port, &done, data, size,
                                           CurrentLinuxThreadId(), &target_armed},
                CompletePendingReadFromQueue, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartWriteFinisher(LibXR::Thread& thread, LibXR::WritePort& port,
                        LibXR::Semaphore& done, LibXR::ErrorCode result, const char* name)
{
  thread.Create(WriteFinishContext{&port, &done, result}, FinishPendingWrite, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartBlockingReadCaller(LibXR::Thread& thread, BlockingReadCallContext& ctx,
                             const char* name)
{
  thread.Create<BlockingReadCallContext*>(&ctx, BlockingReadCall, name, 1024,
                                          LibXR::Thread::Priority::MEDIUM);
}

void StartBlockingWriteCaller(LibXR::Thread& thread, BlockingWriteCallContext& ctx,
                              const char* name)
{
  thread.Create<BlockingWriteCallContext*>(&ctx, BlockingWriteCall, name, 1024,
                                           LibXR::Thread::Priority::MEDIUM);
}

}  // namespace
