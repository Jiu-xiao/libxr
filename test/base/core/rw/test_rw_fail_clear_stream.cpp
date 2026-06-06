/**
 * @file test_rw_fail_clear_stream.cpp
 * @brief base `FailAndClearAll` 队列清理与 stream 锁场景子测试。 Split test unit for base `FailAndClearAll` queue-cleanup and stream-lock scenarios.
 * @details 测试项目：
 *          1. 空闲写口上的 `FailAndClearAll` 会清空残留队列数据和 info block。
 *          2. 活跃 `Stream` 持锁期间调用 `FailAndClearAll` 不会错误解锁，提交后仍能按原数据落队。
 *          Test items:
 *          1. `FailAndClearAll` clears queued data and info blocks on an idle write port.
 *          2. `FailAndClearAll` does not unlock an active `Stream`; commit still flushes the original payload.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_clears_idle_queue`。 Test entry function `test_rw_write_port_fail_and_clear_all_clears_idle_queue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_clears_idle_queue()
{
  // 测试内容：空闲写口的残留队列应在失败清理后被完整清空。
  // Test coverage: residual queued bytes on an idle write port should be fully cleared by fail-clear.
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
  // 测试内容：活跃 stream 持锁期间的失败清理不能把写口提前释放给其他请求。
  // Test coverage: fail-clear during an active stream lock must not release the write port early to other requests.
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

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearStreamTests`。 Test-item function `RunBaseRwFailAndClearStreamTests`.
 * @details 测试内容：执行 `FailAndClearAll` 队列清理与 stream 锁子场景。 Execute `FailAndClearAll` queue-cleanup and stream-lock sub-scenarios.
 *          测试原理：把“空闲清理”和“活跃 stream 锁保持”放在同组，聚焦写口内部状态约束。 Group idle cleanup and active stream-lock preservation around write-port state constraints.
 */
void RunBaseRwFailAndClearStreamTests()
{
  test_rw_write_port_fail_and_clear_all_clears_idle_queue();
  test_rw_write_port_fail_and_clear_all_does_not_unlock_active_stream();
}
