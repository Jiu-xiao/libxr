/**
 * @file test_runtime.cpp
 * @brief runtime 平面 `Topic` 订阅等待测试。 Runtime-plane subscriber waiting tests for `Topic`.
 *
 * 测试项目 / Test items:
 * 1. ASyncSubscriber 的 fresh wait 语义。 Async subscriber wait semantics: verify `StartWaiting()` only arms fresh notifications and preserves timestamp/data for the next publish.
 * 2. SyncSubscriber 的阻塞等待、忙重入和 callback-context 唤醒。 Sync subscriber wait semantics: verify blocking waits, busy reentry rejection, timeout behavior and callback-context publish wakeups.
 *
 * 测试原理 / Test principles:
 * 1. 使用真实 runtime 线程驱动阻塞等待，因为这些语义在 base 平面不存在。 Use real runtime threads for the blocking wait path, because these semantics do not exist on the base plane alone.
 * 2. 同时检查 wait 返回码和收到的数据/时间戳，保证同步和数据传递一起被验证。 Check both wait return codes and received payload/timestamp values so synchronization and data delivery are validated together.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
struct WaitContext
{
  LibXR::Topic::SyncSubscriber<int>* subscriber;
  LibXR::ErrorCode result;
};

/**
 * @brief 辅助函数 `WaitForSyncSubscriber`。 Helper function `WaitForSyncSubscriber`.
 * @details 测试内容：在 LibXR 线程中执行同步订阅等待。 Execute sync-subscriber waiting inside a LibXR thread.
 *          测试原理：只通过 LibXR thread API 构造等待者，避免测试体依赖宿主线程库。 Use only LibXR's thread API to construct the waiter, avoiding host-thread-library dependencies in the test body.
 */
void WaitForSyncSubscriber(WaitContext* ctx)
{
  ctx->result = ctx->subscriber->Wait(200);
}

/**
 * @brief 测试项函数 `TestASyncSubscriberFreshWait`。 Test-item function `TestASyncSubscriberFreshWait`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestASyncSubscriberFreshWait()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  auto domain = LibXR::Topic::Domain("message_async_wait_domain");
  auto topic = LibXR::Topic::CreateTopic<int>("message_async_wait_tp", &domain);
  auto suber = LibXR::Topic::ASyncSubscriber<int>(topic);

  int value = 41;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7041));
  ASSERT(!suber.Available());

  suber.StartWaiting();
  value = 42;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7042));
  ASSERT(suber.Available());
  suber.StartWaiting();
  ASSERT(suber.Available());
  ASSERT(suber.GetData() == value);
  ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7042);
  ASSERT(!suber.Available());

  suber.StartWaiting();
  UNUSED(suber.GetData());
  value = 43;
  topic.PublishFromCallback(value, LibXR::MicrosecondTimestamp(7043), false);
  ASSERT(suber.Available());
  ASSERT(suber.GetData() == value);
  ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7043);
  ASSERT(!suber.Available());
}

/**
 * @brief 测试项函数 `TestSyncSubscriberFreshWait`。 Test-item function `TestSyncSubscriberFreshWait`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestSyncSubscriberFreshWait()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  auto domain = LibXR::Topic::Domain("message_sync_wait_domain");
  auto topic = LibXR::Topic::CreateTopic<int>("message_sync_wait_tp", &domain);
  int rx = -1;
  auto suber = LibXR::Topic::SyncSubscriber<int>(topic, rx);

  for (int i = 0; i < 5; i++)
  {
    auto value = i + 1;
    topic.Publish(value, LibXR::MicrosecondTimestamp(7100 + i));
  }
  ASSERT(rx == -1);
  ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);
  ASSERT(suber.Wait(1) == LibXR::ErrorCode::TIMEOUT);

  auto wait_and_publish = [&](auto publish)
  {
    WaitContext ctx{&suber, LibXR::ErrorCode::FAILED};
    LibXR::Thread wait_thread;
    wait_thread.Create<WaitContext*>(&ctx, WaitForSyncSubscriber, "msg_sync_wait",
                                     1024, LibXR::Thread::Priority::MEDIUM);

    for (uint32_t i = 0;
         i < 1000000 &&
         suber.block_->data_.wait_state.load(std::memory_order_acquire) !=
             LibXR::Topic::SyncBlock::WAITING;
         i++)
    {
      LibXR::Thread::Yield();
    }

    ASSERT(suber.block_->data_.wait_state.load(std::memory_order_acquire) ==
           LibXR::Topic::SyncBlock::WAITING);
    ASSERT(suber.Wait(0) == LibXR::ErrorCode::BUSY);
    publish();
    for (uint32_t i = 0; i < 1000000 && ctx.result != LibXR::ErrorCode::OK; i++)
    {
      LibXR::Thread::Yield();
    }
    LibXR::Thread::Sleep(1);
    ASSERT(ctx.result == LibXR::ErrorCode::OK);
  };

  int value = 6;
  wait_and_publish(
      [&]()
      { topic.PublishFromCallback(value, LibXR::MicrosecondTimestamp(7106), false); });
  ASSERT(rx == value);
  ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7106);
  ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  ASSERT(suber.Wait(1) == LibXR::ErrorCode::TIMEOUT);
  value = 7;
  topic.Publish(value, LibXR::MicrosecondTimestamp(7107));
  ASSERT(rx == 6);
  ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7106);
  ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);

  value = 8;
  wait_and_publish([&]() { topic.Publish(value, LibXR::MicrosecondTimestamp(7108)); });
  ASSERT(rx == value);
  ASSERT(static_cast<uint64_t>(suber.GetTimestamp()) == 7108);
  ASSERT(suber.Wait(0) == LibXR::ErrorCode::TIMEOUT);
}

}  // namespace

/**
 * @brief 测试入口函数 `test_message_runtime`。 Test entry function `test_message_runtime`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_message_runtime()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestASyncSubscriberFreshWait();
  TestSyncSubscriberFreshWait();
}
