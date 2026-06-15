/**
 * @file pipe_transfer_test_common.hpp
 * @brief `Pipe` 传输与模式矩阵测试 helper。 Shared transport and mode-matrix helpers for `Pipe` tests.
 * @details 测试项目：
 *          1. 提供 `Pipe` 延迟写入、样本填充和调用结果断言 helper。
 *          2. 提供读先写后、写先读后与零长度读写场景验证 helper。
 *          3. 提供混合模式与阻塞模式压力场景复用的基础常量和上下文。
 *          Test items:
 *          1. Provide delayed write, sample fill, and call-result assertion helpers for `Pipe`.
 *          2. Provide helpers for read-first, write-first, and zero-length read/write scenarios.
 *          3. Provide reusable constants and contexts for mixed-mode and blocking-mode stress scenarios.
 */
#pragma once

#include <cstring>
#include <vector>

#include "rw_port_test_common.hpp"

namespace
{
constexpr size_t PIPE_CAPACITY = 64;
constexpr size_t MIXED_STRESS_ITERATIONS = 64;
constexpr size_t BLOCK_STRESS_ITERATIONS = 8;

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

}  // namespace
