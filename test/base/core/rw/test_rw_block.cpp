/**
 * @file test_rw_block.cpp
 * @brief base `rw` block/timeout 场景子测试。 Split test unit for base `rw` block and timeout scenarios.
 */
#include "rw_test_common.hpp"



/**
 * @brief 测试入口函数 `test_rw_stream_block_pending_result_propagates`。 Test entry function `test_rw_stream_block_pending_result_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_stream_block_pending_result_propagates()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED,
                                     StreamSubmitMode::COMMIT);
}

void test_rw_stream_block_timeout_detaches_waiter() { VerifyStreamBlockTimeout(); }
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.

/**
 * @brief 测试入口函数 `test_rw_stream_block_destructor_autocommit`。 Test entry function `test_rw_stream_block_destructor_autocommit`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_stream_block_destructor_autocommit()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::OK, LibXR::ErrorCode::OK,
                                     StreamSubmitMode::DESTRUCT);
}

/**
 * @brief 测试入口函数 `test_rw_block_read_timeout_detaches_pending`。 Test entry function `test_rw_block_read_timeout_detaches_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_block_read_timeout_detaches_pending()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  uint8_t timed_out_rx[4] = {0xA1, 0xA2, 0xA3, 0xA4};
  Semaphore sem;
  ReadOperation block_op(sem, 0);

  auto ec = r(RawData{timed_out_rx, sizeof(timed_out_rx)}, block_op);
  ASSERT(ec == ErrorCode::TIMEOUT);

  static const uint8_t STALE_EXPECT[] = {0xA1, 0xA2, 0xA3, 0xA4};
  ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);

  static const uint8_t TX[] = {0x10, 0x20, 0x30, 0x40};
  WriteOperation wop;
  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);
  r.ProcessPendingReads(false);

  ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  ASSERT(sem.Value() == 0);

  uint8_t fresh_rx[sizeof(TX)] = {0};
  ReadOperation rop;
  ec = r(RawData{fresh_rx, sizeof(fresh_rx)}, rop);
  ASSERT(ec == ErrorCode::OK);
  ASSERT(std::memcmp(fresh_rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 测试入口函数 `test_rw_block_write_timeout_detaches_waiter`。 Test entry function `test_rw_block_write_timeout_detaches_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_block_write_timeout_detaches_waiter()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 64);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {1, 2, 3};
  static const uint8_t TX2[] = {4, 5, 6};

  Semaphore sem1;
  WriteOperation op1(sem1, 0);
  auto ec = w(ConstRawData{TX1, sizeof(TX1)}, op1);
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem1.Value() == 0);

  Semaphore sem2;
  WriteOperation op2(sem2, 0);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  ASSERT(ec == ErrorCode::BUSY);
  ASSERT(sem2.Value() == 0);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);
  ASSERT(sem1.Value() == 0);

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem2.Value() == 0);
}

/**
 * @brief 测试入口函数 `test_rw_immediate_error_propagates`。 Test entry function `test_rw_immediate_error_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_immediate_error_propagates()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  for (auto mode : LibXRTest::ALL_MODES)
  {
    ReadPort r(16);
    r = FailReadFun;

    uint8_t rx[1] = {0};
    LibXRTest::ReadHarness read(mode, 0);
    auto ec = r(RawData{rx, sizeof(rx)}, read.op);
    ASSERT(ec == ErrorCode::INIT_ERR);
    if (mode != LibXRTest::TestMode::NONE && mode != LibXRTest::TestMode::BLOCK)
    {
      read.ExpectFinal(ErrorCode::INIT_ERR);
    }
    ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  }

  static const uint8_t TX[] = {0x55};
  for (auto mode : LibXRTest::ALL_MODES)
  {
    WritePort w(2, 16);
    w = FailWriteFun;

    LibXRTest::WriteHarness write(mode, 0);
    auto ec = w(ConstRawData{TX, sizeof(TX)}, write.op);
    ASSERT(ec == ErrorCode::INIT_ERR);
    if (mode != LibXRTest::TestMode::NONE && mode != LibXRTest::TestMode::BLOCK)
    {
      write.ExpectFinal(ErrorCode::INIT_ERR);
    }
    ASSERT(w.Size() == 0);
    ASSERT(w.queue_info_->Size() == 0);
    ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  }
}

/**
 * @brief 测试入口函数 `test_rw_write_port_block_pending_result_propagates`。 Test entry function `test_rw_write_port_block_pending_result_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_block_pending_result_propagates()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x5A};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, ErrorCode::FAILED, "wr_finish");

  auto ec = w(ConstRawData{TX, sizeof(TX)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_block_reused_waiter_discards_stale_signal`。 Test entry function `test_rw_write_port_block_reused_waiter_discards_stale_signal`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_block_reused_waiter_discards_stale_signal()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x6B};
  static const uint8_t TX2[] = {0x7C};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done1;
  Thread finisher1;
  StartWriteFinisher(finisher1, w, done1, ErrorCode::FAILED, "wr_stale1");

  auto ec = w(ConstRawData{TX1, sizeof(TX1)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done1, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher1);
  ASSERT(sem.Value() == 0);

  // Reuse is valid only after the prior BLOCK call returned and drained any
  // late completion token that still belonged to that waiter.
  Semaphore done2;
  Thread finisher2;
  StartWriteFinisher(finisher2, w, done2, ErrorCode::OK, "wr_stale2");

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done2, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher2);
  ASSERT(sem.Value() == 0);
}

/**
 * @brief 测试项函数 `RunBaseRwBlockTests`。 Test-item function `RunBaseRwBlockTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBaseRwBlockTests()
{
  test_rw_stream_block_pending_result_propagates();
  test_rw_stream_block_timeout_detaches_waiter();
  test_rw_stream_block_destructor_autocommit();
  test_rw_block_read_timeout_detaches_pending();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_immediate_error_propagates();
  test_rw_write_port_block_pending_result_propagates();
  test_rw_write_port_block_reused_waiter_discards_stale_signal();
}
