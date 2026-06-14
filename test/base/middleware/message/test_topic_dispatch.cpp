/**
 * @file test_topic_dispatch.cpp
 * @brief 类型化 `Topic` 分发路径子测试。 Split test unit for typed `Topic` dispatch scenarios.
 * @details 测试项目：
 *          1. async、队列和 callback 订阅者 fan-out。
 *          2. callback-context 发布保持时间戳和 ISR 语义。
 *          3. 非平凡 payload 的 typed 传输。
 *          Test items:
 *          1. Fan-out to async, queued, and callback subscribers.
 *          2. Callback-context publish preserves timestamp and ISR semantics.
 *          3. Typed delivery of non-trivial payloads.
 */
#include "topic_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestTopicSubscriberDispatch`。 Test-item function `TestTopicSubscriberDispatch`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestTopicSubscriberDispatch()
{
  // 测试内容：验证同一发布在不同订阅者类型上的 fan-out 与时间戳/ISR 语义。
  // Test coverage: verify fan-out of one publish across subscriber types plus timestamp/ISR semantics.
  ASSERT(LibXR::Topic::Find("missing_default_topic") == nullptr);

  auto domain = LibXR::Topic::Domain("message_topic_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("message_topic_tp", &domain);
  static double msg[4];
  auto async_suber = LibXR::Topic::ASyncSubscriber<double>(topic);
  LibXR::SPSCQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);
  LibXR::SPSCQueue<LibXR::Topic::Message<double>> timed_msg_queue(10);
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

}  // namespace

/**
 * @brief 测试项函数 `RunTopicDispatchTests`。 Test-item function `RunTopicDispatchTests`.
 * @details 测试内容：执行类型化 `Topic` 分发子场景。 Execute typed `Topic` dispatch sub-scenarios.
 *          测试原理：把 fan-out 与时间戳语义单独成组，避免与可变 payload/丢包场景缠在一起。 Group fan-out and timestamp semantics away from mutable-payload and drop scenarios.
 */
void RunTopicDispatchTests()
{
  TestTopicSubscriberDispatch();
}
