/**
 * @file rw_thread_test_common.hpp
 * @brief 读写测试共用的辅助线程和等待函数。 /
 * Shared worker and waiting helpers for read/write tests.
 *
 * 辅助线程供给读取数据，或模拟后端完成、拒绝写入。
 * Workers supply read data or simulate successful and failed backend writes.
 */

#pragma once

#include "rw_mode_test_common.hpp"
#include "test_assert.hpp"

namespace
{
using LibXRTest::ALL_MODES;
using LibXRTest::ASYNC_MODES;
using LibXRTest::ASYNC_TIMEOUT_MS;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::WriteHarness;

inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  TEST_ASSERT(thread.Join() == LibXR::ErrorCode::OK);
}

inline void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = ASYNC_TIMEOUT_MS)
{
  TEST_ASSERT(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

struct ReadQueueCompletionContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  const uint8_t* data;
  size_t size;
};

// 此线程只负责供给数据；done 表示 Publish 已返回，调用方还需 Join 后再释放上下文。
// This worker supplies bytes; done means Publish returned. Join before releasing its
// context.
void CompletePendingReadFromQueue(ReadQueueCompletionContext ctx)
{
  auto queue = ctx.port->GetReadQueue(false);
  auto ans = queue.PushBatch(ctx.data, ctx.size);
  TEST_ASSERT(ans == LibXR::ErrorCode::OK);
  queue.Publish();
  ctx.done->Post();
}

struct WriteFinishContext
{
  LibXR::WritePort* port;
  LibXR::Semaphore* done;
  LibXR::ErrorCode result;
};

// 等待请求提交，再模拟后端消费或报错；队列对象析构时才结算完成通知。
// Wait for submission, then consume or fail it; queue destruction settles completion.
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
      TEST_ASSERT(queue.AvailableSize() <= sizeof(sink));
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

}  // namespace
