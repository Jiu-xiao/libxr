/**
 * @file test_rw_read_queue.cpp
 * @brief base `rw` 读队列与零长度场景子测试。 Split test unit for base `rw` read-queue and zero-length scenarios.
 */
#include "rw_test_common.hpp"

/**
 * @brief 测试入口函数 `test_rw_read_port_clear_queued_data_clears_idle_queue`。 Test entry function `test_rw_read_port_clear_queued_data_clears_idle_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_clear_queued_data_clears_idle_queue()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x21, 0x43, 0x65, 0x87};
  ASSERT(r.queue_data_->PushBatch(TX, sizeof(TX)) == ErrorCode::OK);
  ASSERT(r.Size() == sizeof(TX));

  ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  ASSERT(r.Size() == 0);
  ASSERT(r.dequeue_count == 1);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

/**
 * @brief 测试入口函数 `test_rw_read_port_clear_queued_data_clears_event_queue`。 Test entry function `test_rw_read_port_clear_queued_data_clears_event_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_clear_queued_data_clears_event_queue()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x12, 0x34, 0x56};
  ASSERT(r.queue_data_->PushBatch(TX, sizeof(TX)) == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::EVENT);

  ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  ASSERT(r.Size() == 0);
  ASSERT(r.dequeue_count == 1);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

/**
 * @brief 测试入口函数 `test_rw_read_port_clear_queued_data_busy_pending_read`。 Test entry function `test_rw_read_port_clear_queued_data_busy_pending_read`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_clear_queued_data_busy_pending_read()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  TrackingReadPort r(16);
  r = PendingReadFun;

  uint8_t queued = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&queued, 1) == ErrorCode::OK);

  uint8_t rx[2] = {0xA1, 0xA2};
  ReadHarness read(TestMode::POLLING);
  ASSERT(r(RawData{rx, sizeof(rx)}, read.op) == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  ASSERT(r.ClearQueuedData() == ErrorCode::BUSY);
  ASSERT(r.Size() == 1);
  ASSERT(r.dequeue_count == 0);

  r.FailAndClearAll(ErrorCode::INIT_ERR, false);
  read.ExpectFinal(ErrorCode::INIT_ERR);
  ASSERT(r.Size() == 0);
}

/**
 * @brief 辅助函数 `VerifyPendingWriteFailAndClearMode`。 Helper function `VerifyPendingWriteFailAndClearMode`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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

/**
 * @brief 测试入口函数 `test_rw_zero_read_pending_notifies_without_dequeue`。 Test entry function `test_rw_zero_read_pending_notifies_without_dequeue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_zero_read_pending_notifies_without_dequeue()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  for (auto mode : LibXRTest::ALL_MODES)
  {
    TrackingReadPort r(16);
    r = PendingReadFun;

    uint8_t dummy = 0xA0;
    ReadHarness read(mode);
    Semaphore done;
    Thread finisher;

    static const uint8_t TX[] = {0x31, 0x32};
    StartReadQueueCompleter(finisher, r, done, TX, sizeof(TX), "rd_zero_ready");

    auto ec = r(RawData{&dummy, 0}, read.op);
    ASSERT(ec == ErrorCode::OK);
    ExpectWaitOk(done, SHORT_WAIT_MS);
    JoinThreadIfNeeded(finisher);

    if (mode != TestMode::NONE && mode != TestMode::BLOCK)
    {
      read.ExpectFinal(ErrorCode::OK);
    }

    ASSERT(dummy == 0xA0);
    ASSERT(r.dequeue_count == 0);
    ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
    ASSERT(r.Size() == sizeof(TX));

    uint8_t follow_up[sizeof(TX)] = {};
    ReadOperation follow_op;
    ec = r(RawData{follow_up, sizeof(follow_up)}, follow_op);
    ASSERT(ec == ErrorCode::OK);
    ASSERT(std::memcmp(follow_up, TX, sizeof(TX)) == 0);
    ASSERT(r.dequeue_count == 1);
  }
}

/**
 * @brief 测试入口函数 `test_rw_read_port_block_queue_completion_copies_data`。 Test entry function `test_rw_read_port_block_queue_completion_copies_data`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_block_queue_completion_copies_data()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  static const uint8_t TX[] = {0x5A};
  uint8_t rx[sizeof(TX)] = {0};
  Semaphore sem;
  ReadOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartReadQueueCompleter(finisher, r, done, TX, sizeof(TX), "rd_queue_block");

  auto ec = r(RawData{rx, sizeof(rx)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
  ASSERT(sem.Value() == 0);
}

/**
 * @brief 测试项函数 `RunBaseRwReadQueueTests`。 Test-item function `RunBaseRwReadQueueTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group related state-machine scenarios together to shrink file size while preserving aggregated entrypoints.
 */
void RunBaseRwReadQueueTests()
{
  test_rw_read_port_clear_queued_data_clears_idle_queue();
  test_rw_read_port_clear_queued_data_clears_event_queue();
  test_rw_read_port_clear_queued_data_busy_pending_read();
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_read_port_block_queue_completion_copies_data();
}
