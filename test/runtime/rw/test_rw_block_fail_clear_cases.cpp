/**
 * @file test_rw_block_fail_clear_cases.cpp
 * @brief runtime `FailAndClearAll` 阻塞等待者场景子测试。 Split test unit for runtime
 * `FailAndClearAll` blocking-waiter scenarios.
 * @details 测试项目：
 *          1. 阻塞读等待者被 `FailAndClearAll` 唤醒后会收到失败结果，且旧缓冲区不被污染。
 *          2. 阻塞写等待者被 `FailAndClearAll`
 * 唤醒后会收到失败结果，后续写入仍能恢复工作。 Test items:
 *          3. 已超时分离的阻塞写可由 `FailAndClearAll` 清理，并安全复用同一信号量。
 *          1. A blocking read waiter is failed by `FailAndClearAll` without corrupting
 * the stale buffer.
 *          2. A blocking write waiter is failed by `FailAndClearAll`, and later writes
 * still recover normally.
 *          3. `FailAndClearAll` releases a detached blocking write and permits safe reuse
 * of the same semaphore.
 *          4. Once `FailAndClearAll` wakes a blocking waiter, it cannot consume a new
 * generation that the waiter publishes before the old drain returns.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

struct WriteFailClearGenerationContext
{
  LibXR::WritePort* port;
  LibXR::ConstRawData second_data;
  LibXR::WriteOperation* second_op;
  LibXR::Semaphore second_published;
  std::atomic<LibXR::ErrorCode> second_submit{LibXR::ErrorCode::FAILED};
  std::atomic<uint32_t> completion_post_count{0};
  std::atomic<uint32_t> idle_release_count{0};
};

void PublishNextWriteBeforeOldDrainReturns(LibXR::WritePort& port,
                                           LibXR::WritePortTestCheckpoint checkpoint,
                                           void* raw)
{
  using namespace LibXR;

  auto& context = *static_cast<WriteFailClearGenerationContext*>(raw);
  ASSERT(&port == context.port);

  if (checkpoint == WritePortTestCheckpoint::BLOCK_AFTER_COMPLETION_POST)
  {
    context.completion_post_count.fetch_add(1, std::memory_order_relaxed);
    ASSERT(context.second_published.Wait(SHORT_WAIT_MS) == ErrorCode::OK);
    return;
  }

  if (checkpoint == WritePortTestCheckpoint::BLOCK_AFTER_IDLE_RELEASE)
  {
    context.idle_release_count.fetch_add(1, std::memory_order_relaxed);
    context.second_submit.store(port(context.second_data, *context.second_op, false),
                                std::memory_order_release);
    context.second_published.Post();
  }
}

/**
 * @brief 测试入口函数 `test_rw_read_port_fail_and_clear_all_fails_block_waiter`。 Test
 * entry function `test_rw_read_port_fail_and_clear_all_fails_block_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_read_port_fail_and_clear_all_fails_block_waiter()
{
  // 测试内容：阻塞读被失败清理打断后，应保持旧缓冲区不变并允许后续继续读取。
  // Test coverage: a blocking read interrupted by fail-clear should keep the stale buffer
  // intact and allow later reads.
  using namespace LibXR;

  ReadPort r(16);

  uint8_t stale_rx[1] = {0xA5};
  Semaphore done;
  BlockingReadCallContext ctx{&r, RawData{stale_rx, sizeof(stale_rx)},
                              BLOCK_OPERATION_TIMEOUT_MS, ErrorCode::FAILED, &done};
  Thread reader;
  StartBlockingReadCaller(reader, ctx, "rd_reset");

  REQUIRE(WaitForBusyState(r, ReadPort::BusyState::PENDING));

  r.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, THREAD_STATE_TIMEOUT_MS);
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
 * @brief 测试入口函数 `test_rw_write_port_fail_and_clear_all_fails_block_waiter`。 Test
 * entry function `test_rw_write_port_fail_and_clear_all_fails_block_waiter`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_rw_write_port_fail_and_clear_all_fails_block_waiter()
{
  // 测试内容：阻塞写被失败清理打断后，应释放端口并允许新的写请求重新进入。
  // Test coverage: a blocking write interrupted by fail-clear should release the port and
  // allow a new write to enter.
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x11, 0x22, 0x33};
  static const uint8_t TX2[] = {0x44, 0x55, 0x66};

  Semaphore done;
  BlockingWriteCallContext ctx{&w, ConstRawData{TX1, sizeof(TX1)},
                               BLOCK_OPERATION_TIMEOUT_MS, ErrorCode::FAILED, &done};
  Thread writer;
  StartBlockingWriteCaller(writer, ctx, "wr_reset");

  REQUIRE(WaitForBusyState(w, WritePort::BusyState::BLOCK_WAITING));

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(ctx.result == ErrorCode::INIT_ERR);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, w, finish_done, ErrorCode::OK, "wr_reset_finish");

  Semaphore sem;
  WriteOperation op(sem, BLOCK_OPERATION_TIMEOUT_MS);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, op) == ErrorCode::OK);
  ExpectWaitOk(finish_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(finisher);
}

void test_rw_write_port_fail_and_clear_all_releases_detached_waiter()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;
  static const uint8_t TX1[] = {0x31, 0x32};
  static const uint8_t TX2[] = {0x41, 0x42};
  Semaphore sem;
  WriteOperation timed_out_op(sem, 0);

  ASSERT(w(ConstRawData{TX1, sizeof(TX1)}, timed_out_op) == ErrorCode::TIMEOUT);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::BLOCK_DETACHED);
  ASSERT(sem.Value() == 0);

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);
  ASSERT(sem.Value() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, w, finish_done, ErrorCode::OK, "wr_detached_reset");
  WriteOperation reused_op(sem, BLOCK_OPERATION_TIMEOUT_MS);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, reused_op) == ErrorCode::OK);
  ExpectWaitOk(finish_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
}

void test_rw_write_port_fail_clear_does_not_consume_reentrant_generation()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;
  static const uint8_t FIRST[] = {0x51, 0x52};
  static const uint8_t SECOND[] = {0x61, 0x62, 0x63};

  OperationPollingStatus second_status;
  WriteOperation second_op(second_status);
  WriteFailClearGenerationContext hook_context{&w, ConstRawData{SECOND, sizeof(SECOND)},
                                               &second_op};
  w.SetTestHook(PublishNextWriteBeforeOldDrainReturns, &hook_context);

  Semaphore done;
  BlockingWriteCallContext first_call{&w, ConstRawData{FIRST, sizeof(FIRST)},
                                      BLOCK_OPERATION_TIMEOUT_MS, ErrorCode::FAILED,
                                      &done};
  Thread writer;
  StartBlockingWriteCaller(writer, first_call, "wr_reset_reentry");

  REQUIRE(WaitForBusyState(w, WritePort::BusyState::BLOCK_WAITING));

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);
  ExpectWaitOk(done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  w.SetTestHook(nullptr, nullptr);

  ASSERT(first_call.result == ErrorCode::INIT_ERR);
  ASSERT(hook_context.completion_post_count.load(std::memory_order_relaxed) == 1U);
  ASSERT(hook_context.idle_release_count.load(std::memory_order_relaxed) == 1U);
  ASSERT(hook_context.second_submit.load(std::memory_order_acquire) == ErrorCode::OK);
  ASSERT(second_status.Load() == OperationPollingStatus::RUNNING);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == sizeof(SECOND));
  ASSERT(w.queue_info_->Size() == 1U);

  WriteInfoBlock second_info{};
  uint8_t second_copy[sizeof(SECOND)] = {};
  ASSERT(w.queue_info_->Pop(second_info) == ErrorCode::OK);
  ASSERT(w.queue_data_->PopBatch(second_copy, sizeof(second_copy)) == ErrorCode::OK);
  ASSERT(std::memcmp(second_copy, SECOND, sizeof(SECOND)) == 0);
  w.Finish(false, ErrorCode::OK, second_info);
  ASSERT(second_status.Load() == OperationPollingStatus::DONE);
  ASSERT(w.Size() == 0U);
  ASSERT(w.queue_info_->Size() == 0U);
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeRwBlockFailClearTests`。 Test-item function
 * `RunRuntimeRwBlockFailClearTests`.
 * @details 测试内容：执行 runtime `FailAndClearAll` 阻塞等待者子场景。 Execute runtime
 * `FailAndClearAll` blocking-waiter sub-scenarios.
 *          测试原理：把阻塞等待者语义单独成组，避免和超时/stream 语义互相缠绕。 Group
 * blocking-waiter semantics away from timeout and stream semantics.
 */
void RunRuntimeRwBlockFailClearTests()
{
  test_rw_read_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_releases_detached_waiter();
  test_rw_write_port_fail_clear_does_not_consume_reentrant_generation();
}
