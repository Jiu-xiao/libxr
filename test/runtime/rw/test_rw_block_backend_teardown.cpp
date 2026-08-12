/**
 * @file test_rw_block_backend_teardown.cpp
 * @brief Backend teardown behavior for blocking ReadPort and WritePort operations.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

void test_rw_backend_teardown_fails_block_read_waiter()
{
  using namespace LibXR;

  BackendTeardownReadPort port(16);
  uint8_t stale_rx[1] = {0xA5};
  Semaphore done;
  BlockingReadCallContext context{&port, RawData{stale_rx, sizeof(stale_rx)}, UINT32_MAX,
                                  ErrorCode::FAILED, &done};
  Thread reader;
  StartBlockingReadCaller(reader, context, "rd_reset");

  REQUIRE(WaitForLinuxFutexWait(context.thread_id));
  port.ResetForBackendTeardown(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(reader);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(stale_rx[0] == 0xA5);
  ASSERT(port.Size() == 0);

  uint8_t queued = 0x5A;
  ASSERT(port.queue_data_->PushBatch(&queued, 1) == ErrorCode::OK);
  uint8_t fresh_rx[1] = {0};
  ReadOperation fresh_operation;
  ASSERT(port(RawData{fresh_rx, sizeof(fresh_rx)}, fresh_operation) == ErrorCode::OK);
  ASSERT(fresh_rx[0] == queued);
}

void test_rw_backend_teardown_fails_block_write_waiter()
{
  using namespace LibXR;

  BackendTeardownWritePort port(2, 16);
  port = PendingWriteFun;
  static const uint8_t TX1[] = {0x11, 0x22, 0x33};
  static const uint8_t TX2[] = {0x44, 0x55, 0x66};
  Semaphore done;
  BlockingWriteCallContext context{&port, ConstRawData{TX1, sizeof(TX1)}, UINT32_MAX,
                                   ErrorCode::FAILED, &done};
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_reset");

  REQUIRE(WaitForLinuxFutexWait(context.thread_id));
  port.ResetForBackendTeardown(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(port.Size() == 0);
  ASSERT(port.queue_info_->Size() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, port, finish_done, ErrorCode::OK, "wr_reset_finish");
  Semaphore semaphore;
  WriteOperation operation(semaphore, BLOCK_OPERATION_TIMEOUT_MS);
  ASSERT(port(ConstRawData{TX2, sizeof(TX2)}, operation) == ErrorCode::OK);
  ExpectWaitOk(finish_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(finisher);
}

void test_rw_backend_teardown_releases_detached_write_waiter()
{
  using namespace LibXR;

  BackendTeardownWritePort port(2, 16);
  port = PendingWriteFun;
  static const uint8_t TX1[] = {0x31, 0x32};
  static const uint8_t TX2[] = {0x41, 0x42};
  Semaphore semaphore;
  WriteOperation timed_out_operation(semaphore, 0);

  ASSERT(port(ConstRawData{TX1, sizeof(TX1)}, timed_out_operation) == ErrorCode::TIMEOUT);
  ASSERT(semaphore.Value() == 0);

  WriteOperation rejected_operation;
  ASSERT(port(ConstRawData{TX2, sizeof(TX2)}, rejected_operation) == ErrorCode::BUSY);

  port.ResetForBackendTeardown(ErrorCode::INIT_ERR, false);

  ASSERT(port.Size() == 0);
  ASSERT(port.queue_info_->Size() == 0);
  ASSERT(semaphore.Value() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, port, finish_done, ErrorCode::OK, "wr_detached_reset");
  WriteOperation reused_operation(semaphore, BLOCK_OPERATION_TIMEOUT_MS);
  ASSERT(port(ConstRawData{TX2, sizeof(TX2)}, reused_operation) == ErrorCode::OK);
  ExpectWaitOk(finish_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(semaphore.Value() == 0);
}

}  // namespace

void RunRuntimeRwBlockBackendTeardownTests()
{
  test_rw_backend_teardown_fails_block_read_waiter();
  test_rw_backend_teardown_fails_block_write_waiter();
  test_rw_backend_teardown_releases_detached_write_waiter();
}
