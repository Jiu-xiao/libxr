/**
 * @file test_rw_fail_clear.cpp
 * @brief base `rw` fail-and-clear 场景子测试。 Split test unit for base `rw` fail-and-clear scenarios.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_rw_read_port_fail_and_clear_all_completes_async_pending`。 Test entry function `test_rw_read_port_fail_and_clear_all_completes_async_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_fail_and_clear_all_completes_async_pending()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  for (auto mode : LibXRTest::ASYNC_MODES)
  {
    VerifyPendingReadFailAndClearMode(mode, LibXR::ErrorCode::INIT_ERR);
  }
}

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_completes_async_pending`。 Test entry function `test_rw_write_port_fail_and_clear_all_completes_async_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_completes_async_pending()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  for (auto mode : LibXRTest::ASYNC_MODES)
  {
    VerifyPendingWriteFailAndClearMode(mode, LibXR::ErrorCode::INIT_ERR);
  }
}

/**
 * @brief 测试入口函数 `test_rw_read_port_fail_and_clear_all_fails_block_waiter`。 Test entry function `test_rw_read_port_fail_and_clear_all_fails_block_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_fail_and_clear_all_fails_block_waiter()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t stale_rx[1] = {0xA5};
  Semaphore done;
  BlockingReadCallContext ctx{&r, RawData{stale_rx, sizeof(stale_rx)}, 20,
                              ErrorCode::FAILED, &done};
  Thread reader;
  StartBlockingReadCaller(reader, ctx, "rd_reset");

  while (r.busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
  {
    Thread::Yield();
  }

  r.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(reader);
  ASSERT(ctx.result == ErrorCode::INIT_ERR);
  ASSERT(stale_rx[0] == 0xA5);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(r.Size() == 0);

  uint8_t tx = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&tx, 1) == ErrorCode::OK);

  uint8_t fresh_rx[1] = {0};
  ReadOperation fresh_op;
  ASSERT(r(RawData{fresh_rx, sizeof(fresh_rx)}, fresh_op) == ErrorCode::OK);
  ASSERT(fresh_rx[0] == tx);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_fails_block_waiter`。 Test entry function `test_rw_write_port_fail_and_clear_all_fails_block_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_fails_block_waiter()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x11, 0x22, 0x33};
  static const uint8_t TX2[] = {0x44, 0x55, 0x66};

  Semaphore done;
  BlockingWriteCallContext ctx{&w, ConstRawData{TX1, sizeof(TX1)}, 20, ErrorCode::FAILED,
                               &done};
  Thread writer;
  StartBlockingWriteCaller(writer, ctx, "wr_reset");

  while (w.busy_.load(std::memory_order_acquire) != WritePort::BusyState::BLOCK_WAITING)
  {
    Thread::Yield();
  }

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(ctx.result == ErrorCode::INIT_ERR);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, w, finish_done, ErrorCode::OK, "wr_reset_finish");

  Semaphore sem;
  WriteOperation op(sem, 100);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, op) == ErrorCode::OK);
  ExpectWaitOk(finish_done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_clears_idle_queue`。 Test entry function `test_rw_write_port_fail_and_clear_all_clears_idle_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_clears_idle_queue()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x21, 0x22, 0x23};
  WriteOperation op;
  ASSERT(w(ConstRawData{TX, sizeof(TX)}, op) == ErrorCode::OK);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == sizeof(TX));
  ASSERT(w.queue_info_->Size() == 1);

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);
}

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_does_not_unlock_active_stream`。 Test entry function `test_rw_write_port_fail_and_clear_all_does_not_unlock_active_stream`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_does_not_unlock_active_stream()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x31, 0x32, 0x33, 0x34};
  static const uint8_t BLOCKED_TX[] = {0x41};
  WriteOperation stream_op;
  WritePort::Stream stream(&w, stream_op);

  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::LOCKED);
  ASSERT(stream.Write(ConstRawData{TX, sizeof(TX)}) == ErrorCode::OK);

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::LOCKED);
  ASSERT(w.Size() == sizeof(TX));

  WriteOperation blocked_op;
  ASSERT(w(ConstRawData{BLOCKED_TX, sizeof(BLOCKED_TX)}, blocked_op) == ErrorCode::BUSY);

  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  ASSERT(completed.data.size_ == sizeof(TX));

  uint8_t rx[sizeof(TX)] = {};
  ASSERT(w.queue_data_->PopBatch(rx, sizeof(rx)) == ErrorCode::OK);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);

  w.Finish(false, ErrorCode::OK, completed);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);
}

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearTests`。 Test-item function `RunBaseRwFailAndClearTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBaseRwFailAndClearTests()
{
  test_rw_read_port_fail_and_clear_all_completes_async_pending();
  test_rw_write_port_fail_and_clear_all_completes_async_pending();
  test_rw_read_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_clears_idle_queue();
  test_rw_write_port_fail_and_clear_all_does_not_unlock_active_stream();
}
