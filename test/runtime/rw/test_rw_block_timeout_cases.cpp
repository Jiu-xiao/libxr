/**
 * @file test_rw_block_timeout_cases.cpp
 * @brief RW 超时与后台接收入队测试 / RW timeout and background RX production tests.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

/**
 * @brief 验证超时读不再访问旧缓冲区和信号量
 *        / Verify late RX leaves timed-out read targets unchanged.
 */
void test_rw_block_read_timeout_detaches_pending()
{
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

  ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  ASSERT(sem.Value() == 0);

  uint8_t fresh_rx[sizeof(TX)] = {0};
  ReadOperation rop;
  ec = r(RawData{fresh_rx, sizeof(fresh_rx)}, rop);
  ASSERT(ec == ErrorCode::OK);
  ASSERT(std::memcmp(fresh_rx, TX, sizeof(TX)) == 0);
}

/**
 * @brief 验证零长度读完成后数据仍可读取
 *        / Verify zero-length completion leaves bytes readable.
 */
void test_rw_zero_read_pending_notifies_without_dequeue()
{
  using namespace LibXR;

  for (auto mode : ALL_MODES)
  {
    TrackingReadPort r(16);

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
 * @brief 验证超时写清退后可再次提交且不残留通知
 *        / Verify reuse after timed-out writes retire without posts.
 */
void test_rw_block_write_timeout_detaches_waiter()
{
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

  static uint8_t sink[sizeof(TX1)] = {};
  {
    auto queue = w.GetWriteQueue(false);
    ASSERT(!queue.Empty());
    queue.PopAll(sink);
  }

  Semaphore sem2;
  WriteOperation op2(sem2, 0);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem2.Value() == 0);

  static uint8_t sink2[sizeof(TX2)] = {};
  {
    auto queue = w.GetWriteQueue(false);
    ASSERT(!queue.Empty());
    queue.PopAll(sink2);
  }
  ASSERT(sem1.Value() == 0);
  ASSERT(sem2.Value() == 0);
}

/**
 * @brief 验证后台入队满足阻塞读且不残留通知
 *        / Verify background RX satisfies a BLOCK read without stale tokens.
 */
void test_rw_read_port_block_queue_completion_copies_data()
{
  using namespace LibXR;

  ReadPort r(16);

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
 * @brief 运行 RW 超时与后台接收测试 / Run RW timeout and background RX tests.
 */
void RunRuntimeRwBlockTimeoutTests()
{
  test_rw_block_read_timeout_detaches_pending();
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_read_port_block_queue_completion_copies_data();
}
