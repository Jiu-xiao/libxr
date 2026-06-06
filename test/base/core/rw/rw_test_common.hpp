/**
 * @file rw_test_common.hpp
 * @brief base `rw` / `pipe` 测试共用 helper。 Shared helpers for base `rw` / `pipe` tests.
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
inline constexpr TestMode ASYNC_MODES[] = {TestMode::NONE, TestMode::POLLING,
                                           TestMode::CALLBACK};

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

  ModeHarness(const ModeHarness&) = delete;
  ModeHarness& operator=(const ModeHarness&) = delete;

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
   * @brief 断言辅助函数 `ExpectPendingSubmitted`。 Assertion helper function `ExpectPendingSubmitted`.
   * @details 测试内容：对当前结果施加统一的期望检查。 Apply one unified expectation check to the current result.
   *          测试原理：把重复判定逻辑收口，避免各测试项使用不一致的检查标准。 Concentrate repeated validation logic so test items do not drift to inconsistent checks.
   */
  void ExpectPendingSubmitted() const
  {
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
    if (mode == TestMode::POLLING)
    {
      ASSERT(polling_status == PollingStatus::RUNNING);
    }
    else if (mode == TestMode::CALLBACK)
    {
      ASSERT(probe.count.load(std::memory_order_acquire) == 0);
    }
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
        ASSERT(false);
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
using LibXRTest::ASYNC_MODES;
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

LibXR::ErrorCode FailWriteFun(LibXR::WritePort& port, bool)
{
  LibXR::WriteInfoBlock info;
  auto pop_ans = port.queue_info_->Pop(info);
  if (pop_ans != LibXR::ErrorCode::OK)
  {
    return pop_ans;
  }

  auto drop_ans = port.queue_data_->PopBatch(nullptr, info.data.size_);
  UNUSED(drop_ans);
  ASSERT(drop_ans == LibXR::ErrorCode::OK);
  return LibXR::ErrorCode::INIT_ERR;
}

LibXR::ErrorCode FailReadFun(LibXR::ReadPort&, bool)
{
  return LibXR::ErrorCode::INIT_ERR;
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

struct BlockingWriteExternalOpCallContext
{
  LibXR::WritePort* port;
  LibXR::ConstRawData data;
  LibXR::WriteOperation* op;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

/**
 * @brief 辅助函数 `BlockingWriteExternalOpCall`。 Helper function `BlockingWriteExternalOpCall`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void BlockingWriteExternalOpCall(BlockingWriteExternalOpCallContext* ctx)
{
  ctx->result = (*ctx->port)(ctx->data, *ctx->op);
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

void StartBlockingWriteExternalOpCaller(LibXR::Thread& thread,
                                        BlockingWriteExternalOpCallContext& ctx,
                                        const char* name)
{
  thread.Create<BlockingWriteExternalOpCallContext*>(
      &ctx, BlockingWriteExternalOpCall, name, 1024, LibXR::Thread::Priority::MEDIUM);
}

/**
 * @brief 辅助函数 `VerifyPendingReadMode`。 Helper function `VerifyPendingReadMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void VerifyPendingReadMode(TestMode mode)
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  std::vector<uint8_t> tx = {0x42, 0x73, 0x8A, 0xC1};
  std::vector<uint8_t> rx(4, 0x7A);
  ReadHarness read(mode);
  Semaphore done;
  Thread finisher;
  StartReadQueueCompleter(finisher, r, done, tx.data(), tx.size(), "rd_queue");

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);

  if (mode == TestMode::BLOCK)
  {
    ASSERT(call_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(call_result == ErrorCode::OK);
    read.ExpectPendingSubmitted();
  }

  ExpectWaitOk(done);
  JoinThreadIfNeeded(finisher);
  if (mode != TestMode::NONE && mode != TestMode::BLOCK)
  {
    read.ExpectFinal(ErrorCode::OK);
  }
  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

/**
 * @brief 辅助函数 `VerifyPendingWriteMode`。 Helper function `VerifyPendingWriteMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void VerifyPendingWriteMode(TestMode mode, LibXR::ErrorCode result)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  std::vector<uint8_t> tx = {0x31, 0x41, 0x59, 0x26};
  WriteHarness write(mode);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, result, "wr_finish");

  auto call_result = w(ConstRawData{tx.data(), tx.size()}, write.op);

  if (mode == TestMode::BLOCK)
  {
    ASSERT(call_result == result);
  }
  else
  {
    ASSERT(call_result == ErrorCode::OK);
  }

  ExpectWaitOk(done);
  JoinThreadIfNeeded(finisher);
  if (mode != TestMode::NONE && mode != TestMode::BLOCK)
  {
    write.ExpectFinal(result);
  }
  ASSERT(w.queue_info_->Size() == 0);
}

/**
 * @brief 辅助函数 `VerifyPendingReadFailAndClearMode`。 Helper function `VerifyPendingReadFailAndClearMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void VerifyPendingReadFailAndClearMode(TestMode mode, LibXR::ErrorCode reason)
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t rx[4] = {0x7A, 0x7B, 0x7C, 0x7D};
  static const uint8_t STALE_EXPECT[] = {0x7A, 0x7B, 0x7C, 0x7D};
  ReadHarness read(mode);

  auto call_result = r(RawData{rx, sizeof(rx)}, read.op);
  ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  r.FailAndClearAll(reason, false);

  if (mode != TestMode::NONE)
  {
    read.ExpectFinal(reason);
  }
  ASSERT(std::memcmp(rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(r.Size() == 0);
}

constexpr size_t PIPE_CAPACITY = 64;
constexpr size_t MIXED_STRESS_ITERATIONS = 64;
constexpr size_t BLOCK_STRESS_ITERATIONS = 8;

using LibXRTest::ASYNC_MODES;
using LibXRTest::ExpectWaitOk;
using LibXRTest::JoinThreadIfNeeded;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::WriteHarness;

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

void VerifyPendingReadThenWrite(TestMode read_mode, TestMode write_mode, size_t size,
                                uint8_t seed)
{
  using namespace LibXR;

  Pipe pipe((size > 0) ? size : PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  std::vector<uint8_t> tx(size);
  std::vector<uint8_t> rx(size, 0xCD);
  FillPattern(tx, seed);

  ReadHarness read(read_mode);
  WriteHarness write(write_mode);

  if (read_mode == TestMode::BLOCK)
  {
    Semaphore write_done;
    DelayedPipeWriteContext ctx{
        &w, &write, tx.data(), tx.size(), 5, ErrorCode::FAILED, &write_done};
    Thread writer;
    StartDelayedPipeWriter(writer, ctx, "pipe_write_async");

    auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
    ASSERT(read_result == ErrorCode::OK);
    ExpectWaitOk(write_done);
    JoinThreadIfNeeded(writer);
    ExpectCallResult(write, ctx.result, ErrorCode::OK);
  }
  else
  {
    auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
    ASSERT(read_result == ErrorCode::OK);
    read.ExpectPendingSubmitted();

    auto write_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
    ExpectCallResult(write, write_result, ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void VerifyWriteThenRead(TestMode write_mode, TestMode read_mode, size_t size,
                         uint8_t seed)
{
  using namespace LibXR;

  Pipe pipe((size > 0) ? size : PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  std::vector<uint8_t> tx(size);
  std::vector<uint8_t> rx(size, 0xEF);
  FillPattern(tx, seed);

  ReadHarness read(read_mode);
  WriteHarness write(write_mode);

  auto write_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
  ExpectCallResult(write, write_result, ErrorCode::OK);

  auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
  ExpectCallResult(read, read_result, ErrorCode::OK);

  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  ASSERT(r.Size() == 0);
}

void VerifyPendingWriteFailAndClearMode(TestMode mode, LibXR::ErrorCode reason)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x31, 0x41, 0x59, 0x26};
  WriteHarness write(mode);

  auto call_result = w(ConstRawData{TX, sizeof(TX)}, write.op);
  ASSERT(call_result == ErrorCode::OK);
  write.ExpectPendingSubmitted();

  w.FailAndClearAll(reason, false);

  if (mode != TestMode::NONE)
  {
    write.ExpectFinal(reason);
  }
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);
}

void VerifyZeroWriteMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  WriteHarness write(mode);
  auto write_result = w(ConstRawData{nullptr, 0}, write.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(write_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(write_result == ErrorCode::OK);
    write.ExpectFinal(ErrorCode::OK);
  }
  ASSERT(w.Size() == 0);

  uint8_t tx = 0x5A;
  uint8_t rx = 0;
  WriteOperation plain_write;
  ReadOperation plain_read;
  ASSERT(w(ConstRawData{&tx, 1}, plain_write) == ErrorCode::OK);
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
}

void VerifyZeroReadMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  uint8_t tx = 0xA7;
  WriteOperation write_op;
  ASSERT(w(ConstRawData{&tx, 1}, write_op) == ErrorCode::OK);

  uint8_t dummy = 0x11;
  ReadHarness read(mode);
  auto zero_result = r(RawData{&dummy, 0}, read.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(zero_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(zero_result == ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  uint8_t rx = 0;
  ReadOperation plain_read;
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
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

}  // namespace


void RunBaseRwReadQueueTests();
void RunBaseRwPendingTests();
void RunBaseRwBlockTests();
void RunBaseRwFailAndClearTests();
void RunBasePipeBasicTests();
void RunBasePipeStreamTests();
void RunBasePipeStressTests();
