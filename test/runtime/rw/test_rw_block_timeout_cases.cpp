/**
 * @file test_rw_block_timeout_cases.cpp
 * @brief runtime `rw` 超时、零长度与队列补齐场景子测试。 Split test unit for runtime `rw` timeout, zero-length, and queue-completion scenarios.
 * @details 测试项目：
 *          1. 阻塞读超时后会解除挂起关系，后续补进的数据不会污染旧缓冲区。
 *          2. 零长度挂起读在完成时只发通知，不会偷读队列字节。
 *          3. 阻塞写超时后会解除等待者，后续再次提交仍按新的等待周期工作。
 *          4. 阻塞读在后台补齐队列数据后会把目标字节拷贝到用户缓冲区。
 *          Test items:
 *          1. A blocking read timeout detaches the pending relation and later bytes do not corrupt the stale buffer.
 *          2. A pending zero-length read only triggers completion and does not consume queued bytes.
 *          3. A blocking write timeout detaches the waiter and later submissions start a fresh wait cycle.
 *          4. A blocking read copies bytes into the user buffer after queued data arrives asynchronously.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_block_read_timeout_detaches_pending`。 Test entry function `test_rw_block_read_timeout_detaches_pending`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_block_read_timeout_detaches_pending()
{
  // 测试内容：阻塞读超时后，旧缓冲区和信号量状态都不应被后续补数据污染。
  // Test coverage: later queued bytes must not corrupt the stale buffer or semaphore state after a blocking read timeout.
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
 * @brief 测试入口函数 `test_rw_zero_read_pending_notifies_without_dequeue`。 Test entry function `test_rw_zero_read_pending_notifies_without_dequeue`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_zero_read_pending_notifies_without_dequeue()
{
  // 测试内容：零长度挂起请求完成后，队列字节仍应保留给后续真实读取。
  // Test coverage: queued bytes should remain available for a later real read after a zero-length pending completion.
  using namespace LibXR;

  for (auto mode : ALL_MODES)
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
 * @brief 测试入口函数 `test_rw_block_write_timeout_detaches_waiter`。 Test entry function `test_rw_block_write_timeout_detaches_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_block_write_timeout_detaches_waiter()
{
  // 测试内容：阻塞写超时后，旧等待者不应阻止下一个等待周期重新建立。
  // Test coverage: a timed-out blocking write waiter should not prevent the next wait cycle from being established.
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
 * @brief 测试项函数 `RunRuntimeRwBlockTimeoutTests`。 Test-item function `RunRuntimeRwBlockTimeoutTests`.
 * @details 测试内容：执行 runtime `rw` 超时、零长度与队列补齐子场景。 Execute runtime `rw` timeout, zero-length, and queue-completion sub-scenarios.
 *          测试原理：把等待者解绑与补齐路径单独成组，降低单文件复杂度。 Group waiter-detach and completion paths together to reduce single-file complexity.
 */
void RunRuntimeRwBlockTimeoutTests()
{
  test_rw_block_read_timeout_detaches_pending();
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_read_port_block_queue_completion_copies_data();
}
