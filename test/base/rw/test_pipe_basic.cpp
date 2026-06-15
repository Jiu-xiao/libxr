/**
 * @file test_pipe_basic.cpp
 * @brief base `Pipe` 基础传输场景子测试。 Split test unit for base `Pipe` basic transport scenarios.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_pipe_basic`。 Test entry function `test_pipe_basic`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_basic()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 测试入口函数 `test_pipe_write_then_read`。 Test entry function `test_pipe_write_then_read`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_write_then_read()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {1, 2, 3, 4, 5, 6, 7};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 测试入口函数 `test_pipe_chunked_rw`。 Test entry function `test_pipe_chunked_rw`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_chunked_rw()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX1[] = {'H', 'e', 'l'};
  static const uint8_t TX2[] = {'l', 'o', ' ', 'X', 'R'};
  uint8_t rx[sizeof(TX1) + sizeof(TX2)] = {0};

  ReadOperation rop;
  WriteOperation w1;
  WriteOperation w2;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX1, sizeof(TX1)}, w1);
  ASSERT(ec == ErrorCode::OK);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, w2);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);

  static const uint8_t EXPECT[] = {'H', 'e', 'l', 'l', 'o', ' ', 'X', 'R'};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

/**
 * @brief 测试入口函数 `test_pipe_stream_api`。 Test entry function `test_pipe_stream_api`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_stream_api()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();
  WriteOperation wop;

  uint8_t rx[8] = {0};

  ReadOperation rop;
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0xAA, 0xBB, 0xCC};
  static const uint8_t B[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};
  ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);

  static const uint8_t EXPECT[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

/**
 * @brief 测试项函数 `RunBasePipeBasicTests`。 Test-item function `RunBasePipeBasicTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBasePipeBasicTests()
{
  test_pipe_basic();
  test_pipe_write_then_read();
  test_pipe_chunked_rw();
  test_pipe_stream_api();
}
