/**
 * @file test_rw_block_timeout.cpp
 * @brief base `rw` 超时与立即错误场景子测试。 Split test unit for base `rw` timeout and immediate-error scenarios.
 * @details 测试项目：
 *          1. 阻塞读超时后会解除挂起关系，后续补进的数据不会污染旧缓冲区。
 *          2. 阻塞写超时后会解除等待者，后续再次提交仍按新的等待周期工作。
 *          3. 立即失败的读写回调会在各模式下直接透传错误并复位状态机。
 *          Test items:
 *          1. A blocking read timeout detaches the pending relation and later bytes do not corrupt the stale buffer.
 *          2. A blocking write timeout detaches the waiter and later submissions start a fresh wait cycle.
 *          3. Immediate read/write failures propagate the error directly and reset the state machine in every mode.
 */
#include "rw_test_common.hpp"

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
 * @brief 测试入口函数 `test_rw_immediate_error_propagates`。 Test entry function `test_rw_immediate_error_propagates`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_immediate_error_propagates()
{
  // 测试内容：立即失败路径应在每种模式下直接返回错误，且端口状态恢复为空闲。
  // Test coverage: immediate failure paths should return errors directly in every mode and restore idle port state.
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

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwBlockTimeoutTests`。 Test-item function `RunBaseRwBlockTimeoutTests`.
 * @details 测试内容：执行 base `rw` 超时与立即错误子场景。 Execute base `rw` timeout and immediate-error sub-scenarios.
 *          测试原理：把超时/立即失败路径单独成组，聚焦等待者解绑和错误透传契约。 Group timeout/immediate-failure paths around waiter-detach and error-propagation contracts.
 */
void RunBaseRwBlockTimeoutTests()
{
  test_rw_block_read_timeout_detaches_pending();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_immediate_error_propagates();
}
