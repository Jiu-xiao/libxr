/**
 * @file pipe_transfer_test_common.hpp
 * @brief Pipe 测试共用的数据生成、延后写入和收发检查。 /
 * Shared payload, delayed-write and transfer checks for Pipe tests.
 *
 * 组合不同完成模式，分别执行先读后写和先写后读。
 * Combines completion modes in read-first and write-first transfers.
 */

#pragma once

#include <cstring>
#include <vector>

#include "core/rw/rw_thread_test_common.hpp"
#include "test_assert.hpp"

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
    TEST_ASSERT(call_result == final_result);
  }
  else
  {
    TEST_ASSERT(call_result == LibXR::ErrorCode::OK);
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
    TEST_ASSERT(read_result == ErrorCode::OK);
    ExpectWaitOk(write_done);
    JoinThreadIfNeeded(writer);
    ExpectCallResult(write, ctx.result, ErrorCode::OK);
  }
  else
  {
    auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
    TEST_ASSERT(read_result == ErrorCode::OK);
    read.ExpectPendingSubmitted();

    auto write_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
    ExpectCallResult(write, write_result, ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
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

  TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  TEST_ASSERT(r.Size() == 0);
}

}  // namespace
