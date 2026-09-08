/**
 * @file test_rw_block_timeout.cpp
 * @brief RW 超时、未配置端口与容量错误测试
 *        / RW timeout, unconfigured-port, and capacity tests.
 */
#include "rw_test_common.hpp"
#include "test_assert.hpp"

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
  TEST_ASSERT(ec == ErrorCode::TIMEOUT);

  static const uint8_t STALE_EXPECT[] = {0xA1, 0xA2, 0xA3, 0xA4};
  TEST_ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);

  static const uint8_t TX[] = {0x10, 0x20, 0x30, 0x40};
  WriteOperation wop;
  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  TEST_ASSERT(ec == ErrorCode::OK);
  TEST_ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  TEST_ASSERT(sem.Value() == 0);

  uint8_t fresh_rx[sizeof(TX)] = {0};
  ReadOperation rop;
  ec = r(RawData{fresh_rx, sizeof(fresh_rx)}, rop);
  TEST_ASSERT(ec == ErrorCode::OK);
  TEST_ASSERT(std::memcmp(fresh_rx, TX, sizeof(TX)) == 0);
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
  TEST_ASSERT(ec == ErrorCode::TIMEOUT);
  TEST_ASSERT(sem1.Value() == 0);

  static uint8_t sink[sizeof(TX1)] = {};
  {
    auto queue = w.GetWriteQueue(false);
    TEST_ASSERT(!queue.Empty());
    queue.PopAll(sink);
  }

  Semaphore sem2;
  WriteOperation op2(sem2, 0);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  TEST_ASSERT(ec == ErrorCode::TIMEOUT);
  TEST_ASSERT(sem2.Value() == 0);

  static uint8_t sink2[sizeof(TX2)] = {};
  {
    auto queue = w.GetWriteQueue(false);
    TEST_ASSERT(!queue.Empty());
    queue.PopAll(sink2);
  }
  TEST_ASSERT(sem1.Value() == 0);
  TEST_ASSERT(sem2.Value() == 0);
}

/**
 * @brief 验证未配置端口与容量不足的返回值
 *        / Verify unconfigured-port and capacity errors.
 */
void test_rw_admission_and_capacity_errors()
{
  using namespace LibXR;

  ReadPort unbound(0);
  ReadOperation read_op;
  uint8_t byte = 0;
  TEST_ASSERT(unbound(RawData{&byte, 1}, read_op) == ErrorCode::NOT_SUPPORT);
  TEST_ASSERT(unbound(RawData{nullptr, 0}, read_op) == ErrorCode::NOT_SUPPORT);

  ReadPort read(1);
  TEST_ASSERT(read(RawData{&byte, 2}, read_op) == ErrorCode::SIZE_ERR);
  TEST_ASSERT(read.Size() == 0);

  WritePort unconfigured(2, 1);
  WriteOperation write_op;
  TEST_ASSERT(unconfigured(ConstRawData{&byte, 1}, write_op) == ErrorCode::NOT_SUPPORT);

  WritePort full(2, 1);
  full = PendingWriteFun;
  const uint8_t pair[2] = {0x55, 0x66};
  TEST_ASSERT(full(ConstRawData{pair, sizeof(pair)}, write_op) == ErrorCode::FULL);
  TEST_ASSERT(full.Size() == 0);
}

}  // namespace

/**
 * @brief 运行 RW 超时与接纳错误测试 / Run RW timeout and admission-error tests.
 */
void RunBaseRwBlockTimeoutTests()
{
  test_rw_block_read_timeout_detaches_pending();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_admission_and_capacity_errors();
}
