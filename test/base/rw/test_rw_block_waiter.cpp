/**
 * @file test_rw_block_waiter.cpp
 * @brief base `WritePort` 阻塞等待者场景子测试。 Split test unit for base `WritePort` blocking waiter scenarios.
 * @details 测试项目：
 *          1. 阻塞写等待者在挂起完成后会收到最终失败码。
 *          2. 复用同一个阻塞信号量时，上一轮的陈旧信号不会污染下一轮等待。
 *          Test items:
 *          1. A blocking write waiter receives the final error after pending completion.
 *          2. Reusing the same blocking semaphore does not leak stale signals into the next wait cycle.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_write_port_block_pending_result_propagates`。 Test entry function `test_rw_write_port_block_pending_result_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_block_pending_result_propagates()
{
  // 测试内容：阻塞写在完成线程返回失败时，应把该错误直接传给调用者。
  // Test coverage: a blocking write should forward the exact failure returned by the completion thread.
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
  // 测试内容：复用同一阻塞等待者时，上一轮完成令牌必须被清干净，不能串到下一轮。
  // Test coverage: reusing the same blocking waiter must drain the previous completion token before the next wait cycle.
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

  Semaphore done2;
  Thread finisher2;
  StartWriteFinisher(finisher2, w, done2, ErrorCode::OK, "wr_stale2");

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done2, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher2);
  ASSERT(sem.Value() == 0);
}

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwBlockWaiterTests`。 Test-item function `RunBaseRwBlockWaiterTests`.
 * @details 测试内容：执行 base `WritePort` 阻塞等待者子场景。 Execute base `WritePort` blocking waiter sub-scenarios.
 *          测试原理：把阻塞等待者生命周期单独成组，集中验证最终错误透传和陈旧信号清理。 Group blocking waiter lifecycle checks around final-error propagation and stale-signal draining.
 */
void RunBaseRwBlockWaiterTests()
{
  test_rw_write_port_block_pending_result_propagates();
  test_rw_write_port_block_reused_waiter_discards_stale_signal();
}
