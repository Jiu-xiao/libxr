/**
 * @file test_topic_mutation.cpp
 * @brief 类型化 `Topic` 可变 payload 与队列背压子测试。 Split test unit for typed `Topic` mutable-payload and queue-backpressure scenarios.
 * @details 测试项目：
 *          1. 可变 callback 订阅者能修改调用者可见 payload。
 *          2. 队列订阅者满载时会丢弃后续发布。
 *          Test items:
 *          1. Mutable callback subscribers can modify the caller-visible payload.
 *          2. Queued subscribers drop later publishes when the queue is full.
 */
#include "topic_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestTopicMutationAndQueueDrop`。 Test-item function `TestTopicMutationAndQueueDrop`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestTopicMutationAndQueueDrop()
{
  // 测试内容：验证可变 payload 回写语义，以及队列背压下的溢出丢弃契约。
  // Test coverage: verify mutable-payload writeback semantics and overflow-drop behavior under queue backpressure.
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
 * @brief 测试项函数 `RunTopicMutationTests`。 Test-item function `RunTopicMutationTests`.
 * @details 测试内容：执行类型化 `Topic` 可变 payload 与丢包子场景。 Execute typed `Topic` mutable-payload and drop-behavior sub-scenarios.
 *          测试原理：把 payload 回写和背压丢弃单独成组，聚焦订阅者副作用契约。 Group payload writeback and backpressure-drop behavior around subscriber side-effect contracts.
 */
void RunTopicMutationTests()
{
  TestTopicMutationAndQueueDrop();
}
