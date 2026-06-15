/**
 * @file test_pipe_runtime_cases.cpp
 * @brief runtime `Pipe` 场景子测试。 Split test unit for runtime `Pipe` scenarios.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_pipe_stream_block_immediate_path`。 Test entry function `test_pipe_stream_block_immediate_path`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_stream_block_immediate_path()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  uint8_t rx[8] = {0};
  ReadOperation rop;
  ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  Semaphore sem;
  WriteOperation wop(sem, 100);
  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0x21, 0x22, 0x23};
  static const uint8_t B[] = {0x31, 0x32, 0x33, 0x34, 0x35};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};

  auto ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0x21, 0x22, 0x23, 0x31, 0x32, 0x33, 0x34, 0x35};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

/**
 * @brief 测试入口函数 `test_pipe_block_reuse_stress`。 Test entry function `test_pipe_block_reuse_stress`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_block_reuse_stress()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  constexpr size_t PIPE_CAPACITY = 64;
  constexpr size_t BLOCK_STRESS_ITERATIONS = 8;

  Pipe pipe(PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::BLOCK);
  WriteHarness write(TestMode::BLOCK);

  for (size_t iter = 0; iter < BLOCK_STRESS_ITERATIONS; ++iter)
  {
    const size_t size = 1 + (iter % 15);
    std::vector<uint8_t> tx(size);
    std::vector<uint8_t> rx(size, 0x88);
    FillPattern(tx, static_cast<uint8_t>(0x90 + iter));

    read.Reset();
    write.Reset();

    if ((iter & 1u) == 0)
    {
      Semaphore write_done;
      DelayedPipeWriteContext ctx{
          &w, &write, tx.data(), tx.size(), 5, ErrorCode::FAILED, &write_done};
      Thread writer;
      StartDelayedPipeWriter(writer, ctx, "pipe_block_async");

      ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
      ExpectWaitOk(write_done);
      JoinThreadIfNeeded(writer);
      ExpectCallResult(write, ctx.result, ErrorCode::OK);
    }
    else
    {
      ExpectCallResult(write, w(ConstRawData{tx.data(), tx.size()}, write.op),
                       ErrorCode::OK);
      ExpectCallResult(read, r(RawData{rx.data(), rx.size()}, read.op), ErrorCode::OK);
    }

    ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    ASSERT(r.Size() == 0);
    ASSERT(w.Size() == 0);
  }
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimePipeTests`。 Test-item function `RunRuntimePipeTests`.
 * @details 测试内容：执行当前分组里的 runtime `rw`/`Pipe` 子场景。 Execute the grouped runtime `rw`/`Pipe` sub-scenarios.
 *          测试原理：把 runtime 平面的阻塞和 pipe 场景拆分开，减小单文件复杂度。 Separate runtime blocking and pipe scenarios to reduce single-file complexity.
 */
void RunRuntimePipeTests()
{
  test_pipe_stream_block_immediate_path();
  test_pipe_block_reuse_stress();
}
