/**
 * @file rw_runtime_test_common.hpp
 * @brief runtime `rw` / `pipe` 测试共用 helper。 Shared helpers for runtime `rw` / `pipe` tests.
 */
#pragma once

#include <atomic>
#include <cstring>
#include <vector>

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"

namespace LibXRTest
{

inline constexpr uint32_t ASYNC_TIMEOUT_MS = 200;
inline constexpr uint32_t SHORT_WAIT_MS = 100;

enum class TestMode : uint8_t
{
  NONE,
  POLLING,
  CALLBACK,
  BLOCK
};

inline constexpr TestMode ALL_MODES[] = {TestMode::NONE, TestMode::POLLING,
                                         TestMode::CALLBACK, TestMode::BLOCK};

struct CompletionProbe
{
  std::atomic<uint32_t> count{0};
  std::atomic<int> last{static_cast<int>(LibXR::ErrorCode::OK)};
  LibXR::Semaphore sem;

  CompletionProbe() : sem(0) {}

  /**
   * @brief 辅助函数 `Reset`。 Helper function `Reset`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  void Reset()
  {
    count.store(0, std::memory_order_release);
    last.store(static_cast<int>(LibXR::ErrorCode::OK), std::memory_order_release);
  }
};

template <typename Op>
struct ModeHarness
{
  using CallbackType = typename Op::Callback;
  using PollingStatus = typename Op::OperationPollingStatus;

  explicit ModeHarness(TestMode mode, uint32_t timeout = ASYNC_TIMEOUT_MS)
      : mode(mode), callback(CallbackType::Create(OnCallback, this)), sem(0), op()
  {
    Bind(timeout);
    Reset();
  }

  /**
   * @brief 辅助函数 `Reset`。 Helper function `Reset`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  void Reset()
  {
    polling_status = PollingStatus::READY;
    probe.Reset();
  }

  /**
   * @brief 断言辅助函数 `ExpectFinal`。 Assertion helper function `ExpectFinal`.
   * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
   *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
   */
  void ExpectFinal(LibXR::ErrorCode expected)
  {
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
    switch (mode)
    {
      case TestMode::NONE:
        return;
      case TestMode::POLLING:
        ASSERT(polling_status == ((expected == LibXR::ErrorCode::OK)
                                      ? PollingStatus::DONE
                                      : PollingStatus::ERROR));
        return;
      case TestMode::CALLBACK:
        ASSERT(probe.sem.Wait(ASYNC_TIMEOUT_MS) == LibXR::ErrorCode::OK);
        ASSERT(probe.count.load(std::memory_order_acquire) == 1);
        ASSERT(static_cast<LibXR::ErrorCode>(
                   probe.last.load(std::memory_order_acquire)) == expected);
        return;
      case TestMode::BLOCK:
        return;
    }
  }

  /**
   * @brief 辅助函数 `OnCallback`。 Helper function `OnCallback`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  static void OnCallback(bool in_isr, ModeHarness* self, LibXR::ErrorCode status)
  {
    self->probe.last.store(static_cast<int>(status), std::memory_order_release);
    self->probe.count.fetch_add(1, std::memory_order_acq_rel);
    self->probe.sem.PostFromCallback(in_isr);
  }

  /**
   * @brief 辅助函数 `Bind`。 Helper function `Bind`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  void Bind(uint32_t timeout)
  {
    switch (mode)
    {
      case TestMode::NONE:
        op = Op();
        return;
      case TestMode::POLLING:
        op = Op(polling_status);
        return;
      case TestMode::CALLBACK:
        op = Op(callback);
        return;
      case TestMode::BLOCK:
        op = Op(sem, timeout);
        return;
    }
  }

  TestMode mode;
  PollingStatus polling_status = PollingStatus::READY;
  CompletionProbe probe;
  CallbackType callback;
  LibXR::Semaphore sem;
  Op op;
};

using ReadHarness = ModeHarness<LibXR::ReadOperation>;
using WriteHarness = ModeHarness<LibXR::WriteOperation>;

/**
 * @brief 辅助函数 `JoinThreadIfNeeded`。 Helper function `JoinThreadIfNeeded`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
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

}  // namespace LibXRTest

namespace
{
using LibXRTest::ALL_MODES;
using LibXRTest::ASYNC_TIMEOUT_MS;
using LibXRTest::ExpectWaitOk;
using LibXRTest::JoinThreadIfNeeded;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::WriteHarness;

LibXR::ErrorCode PendingWriteFun(LibXR::WritePort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

LibXR::ErrorCode PendingReadFun(LibXR::ReadPort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

struct ReadQueueCompletionContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  const uint8_t* data;
  size_t size;
};

struct TrackingReadPort : LibXR::ReadPort
{
  using LibXR::ReadPort::ReadPort;
  using LibXR::ReadPort::operator=;

  void OnRxDequeue(bool) override { dequeue_count++; }

  uint32_t dequeue_count = 0;
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

enum class StreamSubmitMode : uint8_t
{
  COMMIT,
  DESTRUCT
};

void VerifyStreamBlockPendingCompletion(LibXR::ErrorCode finish_result,
                                        LibXR::ErrorCode expected_result,
                                        StreamSubmitMode submit_mode)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x41, 0x42, 0x43, 0x44};
  Semaphore sem;
  WriteOperation op(sem, SHORT_WAIT_MS);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(
      finisher, w, done, finish_result,
      (submit_mode == StreamSubmitMode::COMMIT) ? "wr_stream_commit" : "wr_stream_dtor");

  {
    WritePort::Stream ws(&w, op);
    ws << ConstRawData{TX, sizeof(TX)};
    if (submit_mode == StreamSubmitMode::COMMIT)
    {
      auto ec = ws.Commit();
      ASSERT(ec == expected_result);
    }
  }

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

/**
 * @brief 辅助函数 `VerifyStreamBlockTimeout`。 Helper function `VerifyStreamBlockTimeout`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void VerifyStreamBlockTimeout()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x51, 0x52, 0x53};
  Semaphore sem;
  WriteOperation op(sem, 0);
  WritePort::Stream ws(&w, op);
  ws << ConstRawData{TX, sizeof(TX)};

  auto ec = ws.Commit();
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem.Value() == 0);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);

  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

struct DelayedPipeWriteContext
{
  LibXR::WritePort* port;
  WriteHarness* harness;
  const uint8_t* data;
  size_t size;
  uint32_t delay_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

/**
 * @brief 辅助函数 `DelayedPipeWrite`。 Helper function `DelayedPipeWrite`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void DelayedPipeWrite(DelayedPipeWriteContext* ctx)
{
  LibXR::Thread::Sleep(ctx->delay_ms);
  ctx->result = (*ctx->port)(LibXR::ConstRawData{ctx->data, ctx->size}, ctx->harness->op);
  ctx->done->Post();
}

void StartDelayedPipeWriter(LibXR::Thread& thread, DelayedPipeWriteContext& ctx,
                            const char* name)
{
  thread.Create<DelayedPipeWriteContext*>(&ctx, DelayedPipeWrite, name, 1024,
                                          LibXR::Thread::Priority::MEDIUM);
}

/**
 * @brief 辅助函数 `FillPattern`。 Helper function `FillPattern`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void FillPattern(std::vector<uint8_t>& buffer, uint8_t seed)
{
  for (size_t i = 0; i < buffer.size(); ++i)
  {
    buffer[i] = static_cast<uint8_t>(seed + i * 17u + (i % 5u));
  }
}

template <typename Harness>
void ExpectCallResult(Harness& harness, LibXR::ErrorCode call_result,
                      LibXR::ErrorCode final_result)
{
  if (harness.mode == TestMode::BLOCK)
  {
    ASSERT(call_result == final_result);
  }
  else
  {
    ASSERT(call_result == LibXR::ErrorCode::OK);
    harness.ExpectFinal(final_result);
  }
}

}  // namespace


void RunRuntimeRwBlockTests();
void RunRuntimePipeTests();
