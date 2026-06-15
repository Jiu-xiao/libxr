/**
 * @file test_rw_read_queue_pending.cpp
 * @brief base `ReadPort` 队列完成与零长度读场景子测试。 Split test unit for base `ReadPort` queue-completion and zero-length read scenarios.
 * @details 测试项目：
 *          1. 零长度挂起读在完成时只发通知，不会偷读队列字节。
 *          2. 阻塞读在后台补齐队列数据后会把目标字节拷贝到用户缓冲区。
 *          Test items:
 *          1. A pending zero-length read only triggers completion and does not consume queued bytes.
 *          2. A blocking read copies bytes into the user buffer after queued data arrives asynchronously.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_zero_read_pending_notifies_without_dequeue`。 Test entry function `test_rw_zero_read_pending_notifies_without_dequeue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_zero_read_pending_notifies_without_dequeue()
{
  // 测试内容：零长度挂起请求完成后，队列字节仍应保留给后续真实读取。
  // Test coverage: queued bytes should remain available for a later real read after a zero-length pending completion.
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
  // 测试内容：阻塞读在异步补齐后应写回数据且不残留旧信号量状态。
  // Test coverage: a blocking read should copy asynchronously supplied bytes and leave no stale semaphore state.
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

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwReadQueuePendingTests`。 Test-item function `RunBaseRwReadQueuePendingTests`.
 * @details 测试内容：执行读队列完成与零长度请求子场景。 Execute queued-read completion and zero-length request sub-scenarios.
 *          测试原理：把“挂起后完成”的路径单独聚合，减少和清队列语义的耦合。 Group pending-completion paths separately from clear-queue semantics.
 */
void RunBaseRwReadQueuePendingTests()
{
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_read_port_block_queue_completion_copies_data();
}
