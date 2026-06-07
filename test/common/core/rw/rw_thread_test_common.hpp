/**
 * @file rw_thread_test_common.hpp
 * @brief `rw` / `pipe` 线程与等待同步测试 helper。 Shared thread and waiter helpers for `rw` / `pipe` tests.
 * @details 测试项目：
 *          1. 提供线程收尾与等待断言 helper。
 *          2. 提供挂起读写完成、阻塞读写调用的上下文与线程启动封装。
 *          3. 提供把后台补数据/补完成的动作统一包装成可复用 helper。
 *          Test items:
 *          1. Provide thread-settle and wait assertion helpers.
 *          2. Provide contexts and starters for pending completion and blocking read/write calls.
 *          3. Wrap background completion actions into reusable helpers.
 */
#pragma once

#include "rw_mode_test_common.hpp"

namespace
{
using LibXRTest::ALL_MODES;
using LibXRTest::ASYNC_MODES;
using LibXRTest::ASYNC_TIMEOUT_MS;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::WriteHarness;

/**
 * @brief 辅助函数 `JoinThreadIfNeeded`。 Helper function `JoinThreadIfNeeded`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  UNUSED(thread);
  LibXR::Thread::Sleep(1);
}

/**
 * @brief 断言辅助函数 `ExpectWaitOk`。 Assertion helper function `ExpectWaitOk`.
 * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
 *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
 */
inline void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = ASYNC_TIMEOUT_MS)
{
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
  ASSERT(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

struct ReadQueueCompletionContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  const uint8_t* data;
  size_t size;
};

/**
 * @brief 辅助函数 `CompletePendingReadFromQueue`。 Helper function `CompletePendingReadFromQueue`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void CompletePendingReadFromQueue(ReadQueueCompletionContext ctx)
{
  while (ctx.port->busy_.load(std::memory_order_acquire) !=
         LibXR::ReadPort::BusyState::PENDING)
  {
    LibXR::Thread::Yield();
  }

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
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void FinishPendingWrite(WriteFinishContext ctx)
{
  LibXR::WriteInfoBlock completed{};

  while (ctx.port->queue_info_->Pop(completed) != LibXR::ErrorCode::OK)
  {
    LibXR::Thread::Yield();
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
};

/**
 * @brief 辅助函数 `BlockingReadCall`。 Helper function `BlockingReadCall`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void BlockingReadCall(BlockingReadCallContext* ctx)
{
  LibXR::Semaphore sem(0);
  LibXR::ReadOperation op(sem, ctx->timeout_ms);
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
};

/**
 * @brief 辅助函数 `BlockingWriteCall`。 Helper function `BlockingWriteCall`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void BlockingWriteCall(BlockingWriteCallContext* ctx)
{
  LibXR::Semaphore sem(0);
  LibXR::WriteOperation op(sem, ctx->timeout_ms);
  ctx->result = (*ctx->port)(ctx->data, op);
  ctx->done->Post();
}

void StartReadQueueCompleter(LibXR::Thread& thread, LibXR::ReadPort& port,
                             LibXR::Semaphore& done, const uint8_t* data, size_t size,
                             const char* name)
{
  thread.Create(ReadQueueCompletionContext{&port, &done, data, size},
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
