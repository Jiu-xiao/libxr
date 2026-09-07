/**
 * @file rw_thread_test_common.hpp
 * @brief RW 测试线程与等待辅助函数 / Thread and wait helpers for RW tests.
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
 * @brief 等待测试线程结束并检查结果 / Join a test thread and check success.
 */
inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  ASSERT(thread.Join() == LibXR::ErrorCode::OK);
}

/**
 * @brief 检查信号量在指定时间内唤醒 / Check semaphore wakeup within the timeout.
 */
inline void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = ASYNC_TIMEOUT_MS)
{
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
 * @brief 接收入队并发布数据通知 / Queue RX bytes and publish a data notification.
 */
void CompletePendingReadFromQueue(ReadQueueCompletionContext ctx)
{
  auto queue = ctx.port->GetReadQueue(false);
  auto ans = queue.PushBatch(ctx.data, ctx.size);
  ASSERT(ans == LibXR::ErrorCode::OK);
  queue.Publish();
  ctx.done->Post();
}

struct WriteFinishContext
{
  LibXR::WritePort* port;
  LibXR::Semaphore* done;
  LibXR::ErrorCode result;
};

/**
 * @brief 等待队头请求并模拟后端完成
 *        / Wait for a front request and simulate backend completion.
 */
void FinishPendingWrite(WriteFinishContext ctx)
{
  static uint8_t sink[4096];
  for (;;)
  {
    auto queue = ctx.port->GetWriteQueue(false);
    if (queue.Empty())
    {
      LibXR::Thread::Yield();
      continue;
    }

    if (ctx.result == LibXR::ErrorCode::OK)
    {
      ASSERT(queue.AvailableSize() <= sizeof(sink));
      queue.PopAll(sink);
    }
    else
    {
      queue.FailFront(ctx.result);
    }
    break;
  }
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
 * @brief 执行阻塞读并记录返回值 / Execute a blocking read and record its result.
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
 * @brief 执行阻塞写并记录返回值 / Execute a blocking write and record its result.
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
