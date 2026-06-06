/**
 * @file test_pipe_stress.cpp
 * @brief base `Pipe` 压力与模式场景子测试。 Split test unit for base `Pipe` stress and mode scenarios.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_pipe_mode_matrix`。 Test entry function `test_pipe_mode_matrix`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_mode_matrix()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  uint8_t seed = 0x21;

  for (auto read_mode : ASYNC_MODES)
  {
    for (auto write_mode : ASYNC_MODES)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 7, seed++);
      VerifyWriteThenRead(write_mode, read_mode, 7, seed++);
    }
  }
}

/**
 * @brief 测试入口函数 `test_pipe_reuse_stress`。 Test entry function `test_pipe_reuse_stress`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_reuse_stress()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::CALLBACK);
  WriteHarness write(TestMode::POLLING);

  for (size_t iter = 0; iter < MIXED_STRESS_ITERATIONS; ++iter)
  {
    const size_t size = 1 + (iter % 31);
    std::vector<uint8_t> tx(size);
    std::vector<uint8_t> rx(size, 0x44);
    FillPattern(tx, static_cast<uint8_t>(0x40 + iter));

    read.Reset();
    write.Reset();

    if ((iter & 1u) == 0)
    {
      ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
      read.ExpectPendingSubmitted();
      ExpectCallResult(write, w(ConstRawData{tx.data(), tx.size()}, write.op),
                       ErrorCode::OK);
      read.ExpectFinal(ErrorCode::OK);
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

/**
 * @brief 测试入口函数 `test_pipe_edge_cases`。 Test entry function `test_pipe_edge_cases`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_edge_cases()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  for (auto read_mode : ASYNC_MODES)
  {
    for (auto write_mode : ASYNC_MODES)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 1, 0x61);
      VerifyWriteThenRead(write_mode, read_mode, PIPE_CAPACITY, 0x91);
    }
  }
}

/**
 * @brief 测试项函数 `RunBasePipeStressTests`。 Test-item function `RunBasePipeStressTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBasePipeStressTests()
{
  test_pipe_mode_matrix();
  test_pipe_reuse_stress();
  test_pipe_block_reuse_stress();
  test_pipe_edge_cases();
}
