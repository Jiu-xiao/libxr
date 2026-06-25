/**
 * @file test_mixed_modes.cpp
 * @brief `LinuxSharedTopic` domain/mixed-mode 子验证。 Split verification unit for `LinuxSharedTopic` domain and mixed-mode semantics.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
/**
 * @brief 测试项函数 `RunMixedModeScenarios`。 Test-item function `RunMixedModeScenarios`.
 * @details 测试内容：验证 domain 隔离、broadcast 与 balanced 混合共存，以及 balanced group 不可服务时的整体失败语义。 Execute domain-isolation, mixed broadcast/balanced coexistence, and balanced-group-required failure scenarios.
 *          测试原理：这些场景关注的是 topic 分组与模式混搭后的策略边界，而不是单一模式内部行为。 These scenarios focus on policy boundaries created by topic grouping and mixed subscriber modes rather than one mode in isolation.
 */
void RunMixedModeScenarios()
{
  char topic_name[96] = {};
  // The same topic name in different domains must stay isolated.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_domain");

  {
    LibXR::Topic::Domain domain_a("linux_shm_domain_a");
    LibXR::Topic::Domain domain_b("linux_shm_domain_b");

    UNUSED(SharedTopic::Remove(topic_name, domain_a));
    UNUSED(SharedTopic::Remove(topic_name, domain_b));

    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 4;
    config.subscriber_num = 1;
    config.queue_num = 4;

    SharedTopic publisher_a(topic_name, domain_a, config);
    SharedTopic publisher_b(topic_name, "linux_shm_domain_b", config);
    ASSERT(publisher_a.Valid());
    ASSERT(publisher_b.Valid());

    SharedSubscriber subscriber_a(topic_name, "linux_shm_domain_a");
    SharedSubscriber subscriber_b(topic_name, domain_b);
    ASSERT(subscriber_a.Valid());
    ASSERT(subscriber_b.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 331);
    ASSERT(publisher_a.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 441);
    ASSERT(publisher_b.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_a;
    SharedData recv_b;
    ASSERT(subscriber_a.Wait(recv_a, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_a.GetData()->seq == 331);
    ASSERT(recv_b.GetData()->seq == 441);
    recv_a.Reset();
    recv_b.Reset();

    UNUSED(SharedTopic::Remove(topic_name, domain_a));
    UNUSED(SharedTopic::Remove(topic_name, domain_b));
  }
  // Broadcast subscribers and balanced subscribers should coexist on one topic.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_mixed_modes");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 8;
    config.subscriber_num = 3;
    config.queue_num = 4;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber_broadcast(topic_name);
    SharedSubscriber subscriber_rr_a(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_rr_b(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_broadcast.Valid());
    ASSERT(subscriber_rr_a.Valid());
    ASSERT(subscriber_rr_b.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 381);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 382);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 383);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData bc0;
    SharedData bc1;
    SharedData bc2;
    ASSERT(subscriber_broadcast.Wait(bc0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_broadcast.Wait(bc1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_broadcast.Wait(bc2, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(bc0.GetData()->seq == 381);
    ASSERT(bc1.GetData()->seq == 382);
    ASSERT(bc2.GetData()->seq == 383);

    SharedData rr_a0;
    SharedData rr_b0;
    SharedData rr_a1;
    ASSERT(subscriber_rr_a.Wait(rr_a0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_rr_b.Wait(rr_b0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_rr_a.Wait(rr_a1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(rr_a0.GetData()->seq == 381);
    ASSERT(rr_b0.GetData()->seq == 382);
    ASSERT(rr_a1.GetData()->seq == 383);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  // If the balanced group exists but cannot accept, publish should fail for everyone.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_rr_group_required");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 4;
    config.subscriber_num = 2;
    config.queue_num = 2;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber_broadcast(topic_name);
    SharedSubscriber subscriber_rr(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_broadcast.Valid());
    ASSERT(subscriber_rr.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 391);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    FillFrame(frame, 392);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::FULL);

    SharedData bc0;
    ASSERT(subscriber_broadcast.Wait(bc0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(bc0.GetData()->seq == 391);

    SharedData rr0;
    ASSERT(subscriber_rr.Wait(rr0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(rr0.GetData()->seq == 391);
  }
}
}  // namespace LinuxShmTopicTest
