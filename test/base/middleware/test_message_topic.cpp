#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

namespace
{

uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

void TestTopicSubscriberDispatch()
{
  ASSERT(LibXR::Topic::Find("missing_default_topic") == nullptr);

  auto domain = LibXR::Topic::Domain("message_topic_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("message_topic_tp", &domain);
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
        ASSERT(message.data != nullptr);
        view_cb_value = *message.data;
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
  ASSERT(TimestampUs(async_suber.GetTimestamp()) == TimestampUs(timestamp0));
  ASSERT(!async_suber.Available());
  ASSERT(msg_queue.Size() == 1);
  double queue_value = 0.0;
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  LibXR::Topic::Message<double> queue_msg;
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(TimestampUs(queue_msg.timestamp) == TimestampUs(timestamp0));
  ASSERT(msg[3] == msg[0]);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp0));
  ASSERT(view_cb_value == msg[0]);
  ASSERT(TimestampUs(view_cb_timestamp) == TimestampUs(timestamp0));
  ASSERT(!cb_in_isr);

  auto byte_stable_topic =
      LibXR::Topic::CreateTopic<ByteStablePayload>("byte_stable_tp", &domain);
  static LibXR::MicrosecondTimestamp byte_stable_view_timestamp;
  static float byte_stable_view_value = 0.0f;
  auto byte_stable_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, const LibXR::Topic::MessageView<ByteStablePayload>& message)
      {
        byte_stable_view_timestamp = message.timestamp;
        ASSERT(message.data != nullptr);
        byte_stable_view_value = message.data->data[2];
      },
      reinterpret_cast<void*>(0));
  byte_stable_topic.RegisterCallback(byte_stable_cb);
  ByteStablePayload byte_stable_tx{1.0f, 2.0f, 3.0f, 4.0f};
  const LibXR::MicrosecondTimestamp byte_stable_timestamp(1501);
  byte_stable_topic.Publish(byte_stable_tx, byte_stable_timestamp);
  ASSERT(byte_stable_view_value == byte_stable_tx.data[2]);
  ASSERT(TimestampUs(byte_stable_view_timestamp) ==
         TimestampUs(byte_stable_timestamp));

  msg[0] = 32.32;
  msg[3] = -1.0f;
  const LibXR::MicrosecondTimestamp timestamp1(2002);
  async_suber.StartWaiting();
  topic.PublishFromCallback(msg[0], timestamp1, true);
  ASSERT(async_suber.Available());
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(TimestampUs(async_suber.GetTimestamp()) == TimestampUs(timestamp1));
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(TimestampUs(queue_msg.timestamp) == TimestampUs(timestamp1));
  ASSERT(msg[3] == msg[0]);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp1));
  ASSERT(cb_in_isr);
  ASSERT(view_cb_value == msg[0]);
  ASSERT(TimestampUs(view_cb_timestamp) == TimestampUs(timestamp1));
}

void TestTopicMutationAndQueueDrop()
{
  auto domain = LibXR::Topic::Domain("message_topic_mutation_domain");

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
  for (size_t i = 0; i < drop_queue.MaxSize(); ++i)
  {
    auto value = static_cast<int>(i);
    queue_drop_topic.Publish(value, LibXR::MicrosecondTimestamp(8000 + i));
  }
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  int dropped_value = -123;
  queue_drop_topic.Publish(dropped_value, LibXR::MicrosecondTimestamp(9009));
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  for (size_t i = 0; i < drop_queue.MaxSize(); ++i)
  {
    int value = 0;
    ASSERT(drop_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == static_cast<int>(i));
  }
  int dropped_message = 0;
  ASSERT(drop_queue.Pop(dropped_message) == LibXR::ErrorCode::EMPTY);
}

}  // namespace

void test_message_topic()
{
  TestTopicSubscriberDispatch();
  TestTopicMutationAndQueueDrop();
}
