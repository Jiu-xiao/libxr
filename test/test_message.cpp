#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

#include <atomic>
#include <thread>

struct ByteStablePayload
{
  float data[4];

  ByteStablePayload() : data{0.0f, 0.0f, 0.0f, 0.0f} {}

  ByteStablePayload(float a, float b, float c, float d) : data{a, b, c, d} {}

  ByteStablePayload(const ByteStablePayload& other)
      : data{other.data[0], other.data[1], other.data[2], other.data[3]}
  {
  }

  ByteStablePayload& operator=(const ByteStablePayload& other)
  {
    data[0] = other.data[0];
    data[1] = other.data[1];
    data[2] = other.data[2];
    data[3] = other.data[3];
    return *this;
  }
};

static_assert(!std::is_trivially_copyable_v<ByteStablePayload>);
static_assert(std::is_trivially_destructible_v<ByteStablePayload>);
static_assert(LibXR::TopicPayload<ByteStablePayload>);

namespace
{

void TestTopicCore()
{
  ASSERT(LibXR::Topic::Find("missing_default_topic") == nullptr);

  auto timestamp_us = [](const LibXR::MicrosecondTimestamp& timestamp)
  { return static_cast<uint64_t>(timestamp); };

  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("test_tp", &domain);
  static double msg[4];
  auto async_suber = LibXR::Topic::ASyncSubscriber<double>(topic);
  LibXR::LockFreeQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);
  LibXR::LockFreeQueue<LibXR::Topic::Message<double>> timed_msg_queue(10);
  auto timed_queue_suber = LibXR::Topic::QueuedSubscriber(topic, timed_msg_queue);
  UNUSED(queue_suber);
  UNUSED(timed_queue_suber);

  static bool cb_in_isr = false;
  static LibXR::MicrosecondTimestamp cb_timestamp;
  static LibXR::MicrosecondTimestamp view_cb_timestamp;
  static double view_cb_value = 0.0;

  auto msg_cb = LibXR::Topic::Callback::Create(
      [](bool in_isr, void*, LibXR::MicrosecondTimestamp timestamp, double& data)
      {
        cb_in_isr = in_isr;
        cb_timestamp = timestamp;
        msg[3] = data;
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(msg_cb);

  auto view_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, const LibXR::Topic::MessageView<double>& message)
      {
        view_cb_timestamp = message.timestamp;
        view_cb_value = message.data;
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(view_cb);

  ASSERT(!async_suber.Available());

  msg[0] = 16.16;
  const LibXR::MicrosecondTimestamp timestamp0(1001);
  async_suber.StartWaiting();
  topic.Publish(msg[0], timestamp0);
  ASSERT(async_suber.Available());
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(timestamp_us(async_suber.GetTimestamp()) == timestamp_us(timestamp0));
  ASSERT(!async_suber.Available());
  ASSERT(msg_queue.Size() == 1);
  double queue_value = 0.0;
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  LibXR::Topic::Message<double> queue_msg;
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(timestamp_us(queue_msg.timestamp) == timestamp_us(timestamp0));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp0));
  ASSERT(view_cb_value == msg[0]);
  ASSERT(timestamp_us(view_cb_timestamp) == timestamp_us(timestamp0));
  ASSERT(!cb_in_isr);

  auto byte_stable_topic =
      LibXR::Topic::CreateTopic<ByteStablePayload>("byte_stable_tp", &domain);
  static LibXR::MicrosecondTimestamp byte_stable_view_timestamp;
  static float byte_stable_view_value = 0.0f;
  auto byte_stable_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, const LibXR::Topic::MessageView<ByteStablePayload>& message)
      {
        byte_stable_view_timestamp = message.timestamp;
        byte_stable_view_value = message.data.data[2];
      },
      reinterpret_cast<void*>(0));
  byte_stable_topic.RegisterCallback(byte_stable_cb);
  ByteStablePayload byte_stable_tx{1.0f, 2.0f, 3.0f, 4.0f};
  const LibXR::MicrosecondTimestamp byte_stable_timestamp(1501);
  byte_stable_topic.Publish(byte_stable_tx, byte_stable_timestamp);
  ASSERT(byte_stable_view_value == byte_stable_tx.data[2]);
  ASSERT(timestamp_us(byte_stable_view_timestamp) ==
         timestamp_us(byte_stable_timestamp));

  msg[0] = 32.32;
  msg[3] = -1.0f;
  const LibXR::MicrosecondTimestamp timestamp1(2002);
  async_suber.StartWaiting();
  topic.PublishFromCallback(msg[0], timestamp1, true);
  ASSERT(async_suber.Available());
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(timestamp_us(async_suber.GetTimestamp()) == timestamp_us(timestamp1));
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(timestamp_us(queue_msg.timestamp) == timestamp_us(timestamp1));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp1));
  ASSERT(cb_in_isr);
  ASSERT(view_cb_value == msg[0]);
  ASSERT(timestamp_us(view_cb_timestamp) == timestamp_us(timestamp1));

  auto mutable_topic = LibXR::Topic::CreateTopic<int>("mutable_payload_tp", &domain);
  auto mutable_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, int& data) { data = 5678; }, reinterpret_cast<void*>(0));
  mutable_topic.RegisterCallback(mutable_cb);
  int mutable_payload = 1234;
  mutable_topic.Publish(mutable_payload, LibXR::MicrosecondTimestamp(7037));
  ASSERT(mutable_payload == 5678);

  auto queue_drop_topic = LibXR::Topic::CreateTopic<int>("queue_drop_tp", &domain);
  LibXR::LockFreeQueue<int> drop_queue(1);
  auto drop_suber = LibXR::Topic::QueuedSubscriber(queue_drop_topic, drop_queue);
  UNUSED(drop_suber);
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    auto value = static_cast<int>(i);
    queue_drop_topic.Publish(value, LibXR::MicrosecondTimestamp(8000 + i));
  }
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  int dropped_value = -123;
  queue_drop_topic.Publish(dropped_value, LibXR::MicrosecondTimestamp(9009));
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    int value = 0;
    ASSERT(drop_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == static_cast<int>(i));
  }
  int dropped_message = 0;
  ASSERT(drop_queue.Pop(dropped_message) == LibXR::ErrorCode::EMPTY);
}

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

void test_message()
{
  TestTopicCore();
  TestASyncSubscriberFreshWait();
  TestSyncSubscriberFreshWait();
}
