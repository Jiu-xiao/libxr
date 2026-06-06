#include <thread>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

void TestASyncSubscriberFreshWait()
{
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

void TestSyncSubscriberFreshWait()
{
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
    LibXR::ErrorCode wait_result = LibXR::ErrorCode::FAILED;
    std::thread wait_thread([&]() { wait_result = suber.Wait(200); });

    for (uint32_t i = 0;
         i < 1000000 &&
         suber.block_->data_.wait_state.load(std::memory_order_acquire) !=
             LibXR::Topic::SyncBlock::WAITING;
         i++)
    {
      std::this_thread::yield();
    }

    ASSERT(suber.block_->data_.wait_state.load(std::memory_order_acquire) ==
           LibXR::Topic::SyncBlock::WAITING);
    ASSERT(suber.Wait(0) == LibXR::ErrorCode::BUSY);
    publish();
    wait_thread.join();
    ASSERT(wait_result == LibXR::ErrorCode::OK);
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

void test_message_runtime()
{
  TestASyncSubscriberFreshWait();
  TestSyncSubscriberFreshWait();
}
