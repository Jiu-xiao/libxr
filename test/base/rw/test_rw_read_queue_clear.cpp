/**
 * @file test_rw_read_queue_clear.cpp
 * @brief base `ReadPort::ClearQueuedData` 场景子测试。 Split test unit for base
 * `ReadPort::ClearQueuedData` scenarios.
 * @details 测试项目：
 *          1. 空闲队列上的 `ClearQueuedData` 会吃掉已有数据并复位 busy 状态。
 *          2. 无挂起读取时，producer 用 `EVENT` 保留已有队列数据的可读事实；后续读取和
 *             清空可以依次消费它们。
 *          3. 读等待挂起期间调用 `ClearQueuedData` 会返回
 * `BUSY`，不会偷吃尚未交付的数据。 Test items:
 *          1. `ClearQueuedData` consumes queued bytes and resets busy state while idle.
 *          2. A producer records `EVENT` for queued bytes when no read is pending; a
 * later read and clear consume those bytes in order.
 *          3. `ClearQueuedData` returns `BUSY` during pending reads and leaves unread
 * bytes intact.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_read_port_clear_queued_data_clears_idle_queue`。 Test
 * entry function `test_rw_read_port_clear_queued_data_clears_idle_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_clear_queued_data_clears_idle_queue()
{
  // 测试内容：空闲读口在清空队列后应回到 `IDLE`，并递增一次 dequeue 计数。
  // Test coverage: an idle read port should return to `IDLE` and record one dequeue after
  // clearing.
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
 * @brief 测试入口函数 `test_rw_read_port_ownerless_data_remains_readable`。 Test entry
 * function `test_rw_read_port_ownerless_data_remains_readable`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_ownerless_data_remains_readable()
{
  // 测试内容：无挂起读取时，producer 以 EVENT 保留队列可读事实，后续读取和清空应正常
  // 消费数据。
  // Test coverage: without a pending read, EVENT carries queued readiness until a later
  // read and clear consume the bytes.
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x12, 0x34, 0x56};
  ASSERT(r.queue_data_->PushBatch(TX, sizeof(TX)) == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::EVENT);
  ASSERT(r.Size() == sizeof(TX));

  uint8_t read_back[2] = {};
  ReadOperation read_op;
  ASSERT(r(RawData{read_back, sizeof(read_back)}, read_op) == ErrorCode::OK);
  ASSERT(std::memcmp(read_back, TX, sizeof(read_back)) == 0);
  ASSERT(r.Size() == 1U);
  ASSERT(r.dequeue_count == 1);

  ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  ASSERT(r.Size() == 0);
  ASSERT(r.dequeue_count == 2);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

/**
 * @brief 测试入口函数 `test_rw_read_port_clear_queued_data_busy_pending_read`。 Test
 * entry function `test_rw_read_port_clear_queued_data_busy_pending_read`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_clear_queued_data_busy_pending_read()
{
  // 测试内容：挂起读请求期间清空操作必须拒绝，并保留尚未送达的数据。
  // Test coverage: clearing during a pending read must be rejected and preserve unread
  // bytes.
  using namespace LibXR;

  TrackingReadPort r(16);

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

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwReadQueueClearTests`。 Test-item function
 * `RunBaseRwReadQueueClearTests`.
 * @details 测试内容：执行 `ClearQueuedData` 相关子场景。 Execute
 * `ClearQueuedData`-focused sub-scenarios.
 *          测试原理：把清队列语义单独聚合，避免与其他读完成路径混在一起。 Group
 * clear-queue semantics separately from other read-completion paths.
 */
void RunBaseRwReadQueueClearTests()
{
  test_rw_read_port_clear_queued_data_clears_idle_queue();
  test_rw_read_port_ownerless_data_remains_readable();
  test_rw_read_port_clear_queued_data_busy_pending_read();
}
