/**
 * @file test_rw_read_queue_clear.cpp
 * @brief 接收队列清空与空间通知测试 / RX queue clearing and space notification tests.
 */
#include "rw_test_common.hpp"

namespace
{

/**
 * @brief 验证清空接收队列后通知空间可用
 *        / Verify clearing RX data reports available space.
 */
void test_rw_read_port_clear_queued_data_clears_idle_queue()
{
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x21, 0x43, 0x65, 0x87};
  {
    auto queue = r.GetReadQueue(false);
    ASSERT(queue.PushBatch(TX, sizeof(TX)) == ErrorCode::OK);
    queue.Publish();
  }
  ASSERT(r.Size() == sizeof(TX));

  ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  ASSERT(r.Size() == 0);
  ASSERT(r.dequeue_count == 1);
}

/**
 * @brief 验证发布后的接收数据可清空并通知空间可用
 *        / Verify published RX data can be cleared with a space notification.
 */
void test_rw_read_port_clear_queued_data_clears_event_queue()
{
  using namespace LibXR;

  TrackingReadPort r(16);

  static const uint8_t TX[] = {0x12, 0x34, 0x56};
  {
    auto queue = r.GetReadQueue(false);
    ASSERT(queue.PushBatch(TX, sizeof(TX)) == ErrorCode::OK);
    queue.Publish();
  }

  ASSERT(r.ClearQueuedData() == ErrorCode::OK);
  ASSERT(r.Size() == 0);
  ASSERT(r.dequeue_count == 1);
}

/**
 * @brief 验证挂起读期间清空被拒绝并保留数据
 *        / Verify pending reads prevent clearing and preserve queued data.
 */
void test_rw_read_port_clear_queued_data_busy_pending_read()
{
  using namespace LibXR;

  TrackingReadPort r(16);
  uint8_t queued = 0x5A;
  {
    auto queue = r.GetReadQueue(false);
    ASSERT(queue.PushBatch(&queued, 1) == ErrorCode::OK);
    queue.Publish();
  }

  uint8_t rx[2] = {0xA1, 0xA2};
  ReadHarness read(TestMode::POLLING);
  ASSERT(r(RawData{rx, sizeof(rx)}, read.op) == ErrorCode::OK);
  read.ExpectPendingSubmitted();

  ASSERT(r.ClearQueuedData() == ErrorCode::BUSY);
  ASSERT(r.Size() == 1);
  ASSERT(r.dequeue_count == 0);

  const uint8_t completed = 0x6B;
  {
    auto queue = r.GetReadQueue(false);
    ASSERT(queue.PushBatch(&completed, 1) == ErrorCode::OK);
    queue.Publish();
  }
  read.ExpectFinal(ErrorCode::OK);
  ASSERT(r.Size() == 0);
}

}  // namespace

/**
 * @brief 运行接收队列清空测试 / Run RX queue clearing tests.
 */
void RunBaseRwReadQueueClearTests()
{
  test_rw_read_port_clear_queued_data_clears_idle_queue();
  test_rw_read_port_clear_queued_data_clears_event_queue();
  test_rw_read_port_clear_queued_data_busy_pending_read();
}
