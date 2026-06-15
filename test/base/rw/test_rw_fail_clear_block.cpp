/**
 * @file test_rw_fail_clear_block.cpp
 * @brief base `FailAndClearAll` 阻塞等待者场景子测试。 Split test unit for base `FailAndClearAll` blocking-waiter scenarios.
 * @details 测试项目：
 *          1. 阻塞读等待者被 `FailAndClearAll` 唤醒后会收到失败结果，且旧缓冲区不被污染。
 *          2. 阻塞写等待者被 `FailAndClearAll` 唤醒后会收到失败结果，后续写入仍能恢复工作。
 *          Test items:
 *          1. A blocking read waiter is failed by `FailAndClearAll` without corrupting the stale buffer.
 *          2. A blocking write waiter is failed by `FailAndClearAll`, and later writes still recover normally.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 测试入口函数 `test_rw_read_port_fail_and_clear_all_fails_block_waiter`。 Test entry function `test_rw_read_port_fail_and_clear_all_fails_block_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_fail_and_clear_all_fails_block_waiter()
{
  // 测试内容：阻塞读被失败清理打断后，应保持旧缓冲区不变并允许后续继续读取。
  // Test coverage: a blocking read interrupted by fail-clear should keep the stale buffer intact and allow later reads.
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
  // 测试内容：阻塞写被失败清理打断后，应释放端口并允许新的写请求重新进入。
  // Test coverage: a blocking write interrupted by fail-clear should release the port and allow a new write to enter.
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

}  // namespace

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearBlockTests`。 Test-item function `RunBaseRwFailAndClearBlockTests`.
 * @details 测试内容：执行 `FailAndClearAll` 阻塞等待者子场景。 Execute `FailAndClearAll` blocking-waiter sub-scenarios.
 *          测试原理：把阻塞等待者语义单独成组，避免和异步完成、stream 锁状态互相缠绕。 Group blocking-waiter semantics away from asynchronous completions and stream-lock semantics.
 */
void RunBaseRwFailAndClearBlockTests()
{
  test_rw_read_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_fails_block_waiter();
}
