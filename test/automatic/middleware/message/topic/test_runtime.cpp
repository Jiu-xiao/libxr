/**
 * @file test_runtime.cpp
 * @brief Topic 订阅等待测试 / Topic subscriber waiting tests.
 *
 * 检查异步结果的取走与重新等待，以及阻塞订阅的超时、唤醒和第二个等待者被拒绝。
 * Check consuming and renewing async waits, plus blocking timeouts, wakeups and rejection
 * of a second waiter.
 */

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{
struct WaitContext
{
  LibXR::Topic::SyncSubscriber<int>* subscriber;
  LibXR::ErrorCode result;
};

void WaitForSyncSubscriber(WaitContext* ctx) { ctx->result = ctx->subscriber->Wait(200); }

void TestASyncSubscriberFreshWait()
{
  // 开始等待前的发布不会成为新结果；取走结果后，可重新等待下一次发布。
  // Publications before waiting are not new results; consuming a result allows another
  // wait.
  auto domain = LibXR::Topic::Domain("message_async_wait_domain");
  auto topic = LibXR::Topic::CreateTopic<int>("message_async_wait_tp", &domain);
  auto suber = LibXR::Topic::ASyncSubscriber<int>(topic);

  int value = 41;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7041));
  TEST_ASSERT(!suber.Available());

  suber.StartWaiting();
  value = 42;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7042));
  TEST_ASSERT(suber.Available());
  suber.StartWaiting();
  TEST_ASSERT(suber.Available());
  TEST_ASSERT(suber.GetData() == value);
  TEST_ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7042);
  TEST_ASSERT(!suber.Available());

  suber.StartWaiting();
  UNUSED(suber.GetData());
  value = 43;
  topic.PublishFromCallback(value, LibXR::MicrosecondTimestamp(7043), false);
  TEST_ASSERT(suber.Available());
  TEST_ASSERT(suber.GetData() == value);
  TEST_ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7043);
  TEST_ASSERT(!suber.Available());
}

void TestSyncSubscriberFreshWait()
{
  // 等待之前和超时之后的消息都不应留作下一次结果；同时只允许一个阻塞等待者。
  // Messages before waiting or after timeout must not become the next result; allow only
  // one blocking waiter.
  auto domain = LibXR::Topic::Domain("message_sync_wait_domain");
  auto topic = LibXR::Topic::CreateTopic<int>("message_sync_wait_tp", &domain);
  int rx = -1;
  auto suber = LibXR::Topic::SyncSubscriber<int>(topic, rx);

  for (int i = 0; i < 5; i++)
  {
    auto value = i + 1;
    topic.Publish(value, LibXR::MicrosecondTimestamp(7100 + i));
  }
  TEST_ASSERT(rx == -1);
  TEST_ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);
  TEST_ASSERT(suber.Wait(1) == LibXR::ErrorCode::TIMEOUT);

  auto wait_and_publish = [&](auto publish)
  {
    WaitContext ctx{&suber, LibXR::ErrorCode::FAILED};
    LibXR::Thread wait_thread;
    wait_thread.Create<WaitContext*>(&ctx, WaitForSyncSubscriber, "msg_sync_wait", 1024,
                                     LibXR::Thread::Priority::MEDIUM);

    for (uint32_t i = 0;
         i < 1000000 && suber.block_->data_.wait_state.load(std::memory_order_acquire) !=
                            LibXR::Topic::SyncBlock::WAITING;
         i++)
    {
      LibXR::Thread::Yield();
    }

    // 确认等待线程已经登记，再检查第二个等待者被拒绝，然后才发布消息。
    // Confirm waiter registration, reject a second waiter, then publish.
    TEST_ASSERT(suber.block_->data_.wait_state.load(std::memory_order_acquire) ==
                LibXR::Topic::SyncBlock::WAITING);
    TEST_ASSERT(suber.Wait(0) == LibXR::ErrorCode::BUSY);
    publish();
    TEST_ASSERT(wait_thread.Join() == LibXR::ErrorCode::OK);
    TEST_ASSERT(ctx.result == LibXR::ErrorCode::OK);
  };

  int value = 6;
  wait_and_publish(
      [&]()
      { topic.PublishFromCallback(value, LibXR::MicrosecondTimestamp(7106), false); });
  TEST_ASSERT(rx == value);
  TEST_ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7106);
  TEST_ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  TEST_ASSERT(suber.Wait(1) == LibXR::ErrorCode::TIMEOUT);
  value = 7;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7107));
  TEST_ASSERT(rx == 6);
  TEST_ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7106);
  TEST_ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  value = 8;
  wait_and_publish([&]() { topic.Publish(value, LibXR::MicrosecondTimestamp(7108)); });
  TEST_ASSERT(rx == value);
  TEST_ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7108);
  TEST_ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);
}

}  // namespace

void test_message_runtime()
{
  TestASyncSubscriberFreshWait();
  TestSyncSubscriberFreshWait();
}
