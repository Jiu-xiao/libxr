/**
 * @file test_rw_read_queue_pending.cpp
 * @brief 零长度读与后台接收入队测试
 *        / Zero-length reads and background RX production tests.
 */
#include "rw_test_common.hpp"
#include "test_assert.hpp"

namespace
{

/**
 * @brief 验证零长度读完成后数据仍可读取
 *        / Verify zero-length completion leaves bytes readable.
 */
void test_rw_zero_read_pending_notifies_without_dequeue()
{
  using namespace LibXR;

  for (auto mode : LibXRTest::ALL_MODES)
  {
    TrackingReadPort r(16);

    uint8_t dummy = 0xA0;
    ReadHarness read(mode);
    Semaphore done;
    Thread finisher;

    static const uint8_t TX[] = {0x31, 0x32};
    StartReadQueueCompleter(finisher, r, done, TX, sizeof(TX), "rd_zero_ready");

    auto ec = r(RawData{&dummy, 0}, read.op);
    TEST_ASSERT(ec == ErrorCode::OK);
    ExpectWaitOk(done, SHORT_WAIT_MS);
    JoinThreadIfNeeded(finisher);

    if (mode != TestMode::NONE && mode != TestMode::BLOCK)
    {
      read.ExpectFinal(ErrorCode::OK);
    }

    TEST_ASSERT(dummy == 0xA0);
    TEST_ASSERT(r.dequeue_count == 0);
    TEST_ASSERT(r.Size() == sizeof(TX));

    uint8_t follow_up[sizeof(TX)] = {};
    ReadOperation follow_op;
    ec = r(RawData{follow_up, sizeof(follow_up)}, follow_op);
    TEST_ASSERT(ec == ErrorCode::OK);
    TEST_ASSERT(std::memcmp(follow_up, TX, sizeof(TX)) == 0);
    TEST_ASSERT(r.dequeue_count == 1);
  }
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
  TEST_ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  TEST_ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
  TEST_ASSERT(sem.Value() == 0);
}

}  // namespace

/**
 * @brief 运行零长度读与后台接收测试 / Run zero-length read and background RX tests.
 */
void RunBaseRwReadQueuePendingTests()
{
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_read_port_block_queue_completion_copies_data();
}
