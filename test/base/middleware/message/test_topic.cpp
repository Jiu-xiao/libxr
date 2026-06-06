/**
 * @file test_topic.cpp
 * @brief 类型化 Topic 发布/订阅分发测试。 Typed topic publish/subscribe dispatch tests.
 *
 * 测试项目 / Test items:
 * 1. 多种订阅者 fan-out。 Subscriber fan-out: verify async, queue and callback subscribers all observe the same publish on the same topic.
 * 2. 非平凡 payload 的 typed 传输。 Byte-stable payload delivery: verify non-trivially-copyable but topic-legal payloads still reach typed callbacks correctly.
 * 3. callback 上下文发布与可变 payload 语义。 Callback-context publish and mutable payload semantics: verify callback-context publish preserves timestamps/ISR state and mutable callback subscribers can modify the caller-visible payload.
 * 4. 队列订阅者的背压丢弃。 Queue backpressure: verify queued subscribers drop overflow publishes once the target queue is full.
 *
 * 测试原理 / Test principles:
 * 1. 通过真实 `Topic` API 逐个观察订阅者类型，因为总线契约定义在聚合分发行为上。 Publish through the real `Topic` API and observe each subscriber flavor independently, because the bus contract is defined by the aggregate fan-out behavior.
 * 2. 同时检查时间戳和 payload 变化，避免只验证字节复制不验证语义。 Check both timestamp metadata and payload value changes so the test covers semantic, not just byte-copy, correctness.
 */
#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

namespace
{

/**
 * @brief 辅助函数 `TimestampUs`。 Helper function `TimestampUs`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

/**
 * @brief 测试项函数 `TestTopicSubscriberDispatch`。 Test-item function `TestTopicSubscriberDispatch`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestTopicSubscriberDispatch()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestTopicMutationAndQueueDrop`。 Test-item function `TestTopicMutationAndQueueDrop`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestTopicMutationAndQueueDrop()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试入口函数 `test_message_topic`。 Test entry function `test_message_topic`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_message_topic()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestTopicSubscriberDispatch();
  TestTopicMutationAndQueueDrop();
}
