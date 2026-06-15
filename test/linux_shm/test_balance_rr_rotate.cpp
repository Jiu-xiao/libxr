/**
 * @file test_balance_rr_rotate.cpp
 * @brief `LinuxSharedTopic` BALANCE_RR 轮转分发子验证。 Split verification unit for BALANCE_RR rotation semantics.
 * @details 测试项目：
 *          1. 活跃 balanced subscriber 之间按轮转顺序交付。
 *          Test items:
 *          1. Delivery rotates in order among active balanced subscribers.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunBalanceRoundRobinRotateScenario()
{
  char topic_name[96] = {};
  // BALANCE_RR should rotate delivery between active balanced subscribers.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_bal_rr");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 4;
    config.subscriber_num = 2;
    config.queue_num = 4;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber_a(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_b(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_a.Valid());
    ASSERT(subscriber_b.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 351);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 352);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 353);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 354);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_a1;
    SharedData recv_a2;
    SharedData recv_b1;
    SharedData recv_b2;

    ASSERT(subscriber_a.Wait(recv_a1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_a.Wait(recv_a2, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b2, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);

    ASSERT(recv_a1.GetData()->seq == 351);
    ASSERT(recv_b1.GetData()->seq == 352);
    ASSERT(recv_a2.GetData()->seq == 353);
    ASSERT(recv_b2.GetData()->seq == 354);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

}  // namespace LinuxShmTopicTest
