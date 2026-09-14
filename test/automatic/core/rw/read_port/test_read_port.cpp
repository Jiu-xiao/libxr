/**
 * @file test_read_port.cpp
 * @brief 检查 ReadPort 的请求提交、完成通知、清空和超时。 /
 * Tests ReadPort admission, completion, clearing and timeouts.
 *
 * 检查零长度读取不消费数据，超时后旧缓冲区和等待信号量不再被修改。
 * Checks non-consuming zero-length reads and untouched buffers and waiters after timeout.
 */

#include <cstring>
#include <vector>

#include "core/rw/rw_thread_test_common.hpp"
#include "test_assert.hpp"

namespace
{
// 记录空间可用通知次数；这个通知不是已读字节数。
// Count space-available notifications, not bytes consumed.
struct TrackingReadPort : LibXR::ReadPort
{
  using LibXR::ReadPort::ReadPort;
  void OnReadQueueSpaceAvailable(bool) override { dequeue_count++; }

  uint32_t dequeue_count = 0;
};

void VerifyPendingReadMode(TestMode mode)
{
  // 先提交读取，再补足数据；非阻塞调用返回 OK 只说明请求已接收。
  // Submit the read before supplying data; nonblocking OK only admits the request.
  using namespace LibXR;

  ReadPort r(16);

  std::vector<uint8_t> tx = {0x42, 0x73, 0x8A, 0xC1};
  std::vector<uint8_t> rx(4, 0x7A);
  ReadHarness read(mode);

  if (mode == TestMode::BLOCK)
  {
    Semaphore done;
    Thread finisher;
    StartReadQueueCompleter(finisher, r, done, tx.data(), tx.size(), "rd_queue");

    auto block_result = r(RawData{rx.data(), rx.size()}, read.op);
    TEST_ASSERT(block_result == ErrorCode::OK);

    ExpectWaitOk(done);
    JoinThreadIfNeeded(finisher);
    TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    return;
  }

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);
  TEST_ASSERT(call_result == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  {
    auto queue = r.GetReadQueue(false);
    TEST_ASSERT(queue.PushBatch(tx.data(), tx.size()) == ErrorCode::OK);
    queue.Publish();
  }
  if (mode != TestMode::NONE)
  {
    read.ExpectFinal(ErrorCode::OK);
  }
  TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
}

void test_rw_read_port_clear_queued_data_clears_idle_queue()
{
  // 没有待完成的读取时，Clear 丢弃已有数据，并通知后端空间可用。
  // With no pending read, Clear discards queued data and notifies the backend of space.
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x21, 0x43, 0x65, 0x87};
  {
    auto queue = r.GetReadQueue(false);
    TEST_ASSERT(queue.PushBatch(TX, sizeof(TX)) == ErrorCode::OK);
    queue.Publish();
  }
  TEST_ASSERT(r.Size() == sizeof(TX));

  TEST_ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  TEST_ASSERT(r.Size() == 0);
  TEST_ASSERT(r.dequeue_count == 1);
}

void test_rw_read_port_clear_queued_data_busy_pending_read()
{
  // 只放入一个字节却申请读两个；Clear 必须返回 BUSY，保留数据和请求。
  // Queue one byte for a two-byte read; Clear must return BUSY and preserve both.
  using namespace LibXR;

  TrackingReadPort r(16);
  uint8_t queued = 0x5A;
  {
    auto queue = r.GetReadQueue(false);
    TEST_ASSERT(queue.PushBatch(&queued, 1) == ErrorCode::OK);
    queue.Publish();
  }

  uint8_t rx[2] = {0xA1, 0xA2};
  ReadHarness read(TestMode::POLLING);
  TEST_ASSERT(r(RawData{rx, sizeof(rx)}, read.op) == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  TEST_ASSERT(r.ClearQueuedData() == ErrorCode::BUSY);
  TEST_ASSERT(r.Size() == 1);
  TEST_ASSERT(r.dequeue_count == 0);

  const uint8_t completed = 0x6B;
  {
    auto queue = r.GetReadQueue(false);
    TEST_ASSERT(queue.PushBatch(&completed, 1) == ErrorCode::OK);
    queue.Publish();
  }
  read.ExpectFinal(ErrorCode::OK);
  TEST_ASSERT(r.Size() == 0);
}

void test_rw_block_read_timeout_detaches_pending()
{
  // 读取超时后再送入数据，旧缓冲区和信号量都不能再被这次请求修改。
  // Supply data after a read times out; its old buffer and semaphore must remain
  // untouched.
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

void test_rw_zero_read_notifies_without_dequeue()
{
  // 零长度读取只等待有数据，不消费数据；生产线程可能先于读取运行。
  // A zero-length read waits for data without consuming it; production may happen first.
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

void test_rw_read_port_block_queue_completion_copies_data()
{
  // 返回成功时数据已复制完成，等待信号量不应残留可被下一次读取消耗的通知。
  // On success the bytes are copied, with no semaphore token left for the next read.
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

void test_read_port()
{
  using namespace LibXR;

  for (auto mode : ASYNC_MODES)
  {
    VerifyPendingReadMode(mode);
  }

  ReadPort unbound(0);
  ReadOperation read_op;
  uint8_t byte = 0;
  TEST_ASSERT(unbound(RawData{&byte, 1}, read_op) == ErrorCode::NOT_SUPPORT);
  TEST_ASSERT(unbound(RawData{nullptr, 0}, read_op) == ErrorCode::NOT_SUPPORT);

  ReadPort read(1);
  TEST_ASSERT(read(RawData{&byte, 2}, read_op) == ErrorCode::SIZE_ERR);
  TEST_ASSERT(read.Size() == 0);

  test_rw_read_port_clear_queued_data_clears_idle_queue();
  test_rw_read_port_clear_queued_data_busy_pending_read();
  test_rw_block_read_timeout_detaches_pending();
  test_rw_zero_read_notifies_without_dequeue();
  test_rw_read_port_block_queue_completion_copies_data();
}
