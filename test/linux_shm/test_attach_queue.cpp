/**
 * @file test_attach_queue.cpp
 * @brief `LinuxSharedTopic` attach/queue 语义子验证。 Split verification unit for `LinuxSharedTopic` attach and queue semantics.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
/**
 * @brief 测试项函数 `RunAttachQueueScenarios`。 Test-item function `RunAttachQueueScenarios`.
 * @details 测试内容：验证本地 attach/backpressure 以及 broadcast 队列模式的基本行为。 Execute the local attach/backpressure and broadcast queue-mode scenarios.
 *          测试原理：把无需跨进程协调的基础共享内存 topic 语义集中到同一场景文件里。 Group the foundational shared-memory topic semantics that do not require complex cross-process coordination into one scenario file.
 */
void RunAttachQueueScenarios()
{
  char topic_name[96] = {};
  // Basic attach-only semantics and slot backpressure.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_local");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 2;
    config.subscriber_num = 2;
    config.queue_num = 4;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name);
    ASSERT(subscriber.Valid());
    ASSERT(publisher.GetSubscriberNum() == 1);

    SharedTopic attach_only(topic_name);
    ASSERT(attach_only.Valid());
    SharedData attach_data;
    ASSERT(attach_only.CreateData(attach_data) == LibXR::ErrorCode::STATE_ERR);

    SharedData data0;
    const LibXR::MicrosecondTimestamp timestamp0(101000);
    ASSERT(publisher.CreateData(data0) == LibXR::ErrorCode::OK);
    FillFrame(*data0.GetData(), 100);
    ASSERT(publisher.Publish(data0, timestamp0) == LibXR::ErrorCode::OK);

    SharedData data1;
    const LibXR::MicrosecondTimestamp timestamp1(102000);
    ASSERT(publisher.CreateData(data1) == LibXR::ErrorCode::OK);
    FillFrame(*data1.GetData(), 101);
    ASSERT(publisher.Publish(data1, timestamp1) == LibXR::ErrorCode::OK);

    SharedData data2;
    ASSERT(publisher.CreateData(data2) == LibXR::ErrorCode::FULL);

    ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 100);
    ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
           static_cast<uint64_t>(timestamp0));
    subscriber.GetData()->seq = 1000;
    ASSERT(subscriber.GetData()->seq == 1000);
    ASSERT(subscriber.GetPendingNum() == 1);
    subscriber.Release();

    const LibXR::MicrosecondTimestamp timestamp2(103000);
    ASSERT(publisher.CreateData(data2) == LibXR::ErrorCode::OK);
    FillFrame(*data2.GetData(), 102);
    ASSERT(publisher.Publish(data2, timestamp2) == LibXR::ErrorCode::OK);

    ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 101);
    ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
           static_cast<uint64_t>(timestamp1));
    subscriber.Release();

    ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 102);
    ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
           static_cast<uint64_t>(timestamp2));
    ASSERT(subscriber.GetPendingNum() == 0);
    subscriber.Release();
  }
  // BROADCAST_FULL should fail publish when the subscriber queue is saturated.
  UNUSED(SharedTopic::Remove(topic_name));
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_queue");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 8;
    config.subscriber_num = 1;
    config.queue_num = 3;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name);
    ASSERT(subscriber.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 201);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 202);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 203);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::FULL);

    ASSERT(subscriber.GetPendingNum() == 2);
    ASSERT(subscriber.GetDropNum() == 1);
    ASSERT(publisher.GetPublishFailedNum() == 1);

    SharedData recv_data;
    ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 1);
    AssertFrame(*recv_data.GetData(), 201);
    const SharedData& const_recv_data = recv_data;
    const_recv_data.GetData()->seq = 1201;
    ASSERT(const_recv_data.GetData()->seq == 1201);
    recv_data.Reset();

    ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 2);
    AssertFrame(*recv_data.GetData(), 202);
    recv_data.Reset();
  }

  // BROADCAST_DROP_OLD should keep the newest descriptors without failing publish.
  UNUSED(SharedTopic::Remove(topic_name));
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_drop_old");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 8;
    config.subscriber_num = 1;
    config.queue_num = 3;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name, LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD);
    ASSERT(subscriber.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 211);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 212);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 213);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    ASSERT(subscriber.GetPendingNum() == 2);
    ASSERT(subscriber.GetDropNum() == 1);
    ASSERT(publisher.GetPublishFailedNum() == 0);

    SharedData recv_data;
    ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 2);
    AssertFrame(*recv_data.GetData(), 212);
    recv_data.Reset();

    ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 3);
    AssertFrame(*recv_data.GetData(), 213);
    recv_data.Reset();
  }
}
}  // namespace LinuxShmTopicTest
