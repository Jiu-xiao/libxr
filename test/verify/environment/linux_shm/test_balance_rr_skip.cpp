/**
 * @file test_balance_rr_skip.cpp
 * @brief `LinuxSharedTopic` BALANCE_RR 跳过满队列成员子验证。 Split verification unit for BALANCE_RR full-member skipping semantics.
 * @details 测试项目：
 *          1. 某个 balanced subscriber 满时会跳过它并投递给其他成员。
 *          Test items:
 *          1. When one balanced subscriber is full, delivery skips it and goes to others.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunBalanceRoundRobinSkipFullScenario()
{
  char topic_name[96] = {};
  // BALANCE_RR should skip a full member when another balanced subscriber can accept.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_bal_rr_skip_full");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 8;
    config.subscriber_num = 3;
    config.queue_num = 2;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber_a(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_b(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_c(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_a.Valid());
    ASSERT(subscriber_b.Valid());
    ASSERT(subscriber_c.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 371);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 372);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 373);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_b0;
    ASSERT(subscriber_b.Wait(recv_b0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_b0.GetData()->seq == 372);
    recv_b0.Reset();

    FillFrame(frame, 374);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_a;
    SharedData recv_b1;
    SharedData recv_c;
    ASSERT(subscriber_a.Wait(recv_a, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_c.Wait(recv_c, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);

    ASSERT(recv_a.GetData()->seq == 371);
    ASSERT(recv_b1.GetData()->seq == 374);
    ASSERT(recv_c.GetData()->seq == 373);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

}  // namespace LinuxShmTopicTest
