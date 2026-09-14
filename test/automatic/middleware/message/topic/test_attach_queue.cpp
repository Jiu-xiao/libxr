/**
 * @file test_attach_queue.cpp
 * @brief 共享 Topic 的连接与队列测试 / Shared Topic attachment and queue tests.
 *
 * 检查 LinuxSharedTopic 的连接、槽位占用、满队列处理和旧唤醒提示。
 * Check LinuxSharedTopic attachment, slot occupancy, full queues and stale wake hints.
 */

#include <thread>

#include "linux_shm_topic_test_common.hpp"
#include "test_assert.hpp"

namespace LinuxShmTopicTest
{
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
    TEST_ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name);
    TEST_ASSERT(subscriber.Valid());
    TEST_ASSERT(publisher.GetSubscriberNum() == 1);

    SharedTopic attach_only(topic_name);
    TEST_ASSERT(attach_only.Valid());
    SharedData attach_data;
    TEST_ASSERT(attach_only.CreateData(attach_data) == LibXR::ErrorCode::STATE_ERR);

    SharedData data0;
    const LibXR::MicrosecondTimestamp timestamp0(101000);
    TEST_ASSERT(publisher.CreateData(data0) == LibXR::ErrorCode::OK);
    FillFrame(*data0.GetData(), 100);
    TEST_ASSERT(publisher.Publish(data0, timestamp0) == LibXR::ErrorCode::OK);

    SharedData data1;
    const LibXR::MicrosecondTimestamp timestamp1(102000);
    TEST_ASSERT(publisher.CreateData(data1) == LibXR::ErrorCode::OK);
    FillFrame(*data1.GetData(), 101);
    TEST_ASSERT(publisher.Publish(data1, timestamp1) == LibXR::ErrorCode::OK);

    SharedData data2;
    TEST_ASSERT(publisher.CreateData(data2) == LibXR::ErrorCode::FULL);

    TEST_ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 100);
    TEST_ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
                static_cast<uint64_t>(timestamp0));
    subscriber.GetData()->seq = 1000;
    TEST_ASSERT(subscriber.GetData()->seq == 1000);
    TEST_ASSERT(subscriber.GetPendingNum() == 1);
    subscriber.Release();

    const LibXR::MicrosecondTimestamp timestamp2(103000);
    TEST_ASSERT(publisher.CreateData(data2) == LibXR::ErrorCode::OK);
    FillFrame(*data2.GetData(), 102);
    TEST_ASSERT(publisher.Publish(data2, timestamp2) == LibXR::ErrorCode::OK);

    TEST_ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 101);
    TEST_ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
                static_cast<uint64_t>(timestamp1));
    subscriber.Release();

    TEST_ASSERT(subscriber.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 102);
    TEST_ASSERT(static_cast<uint64_t>(subscriber.GetTimestamp()) ==
                static_cast<uint64_t>(timestamp2));
    TEST_ASSERT(subscriber.GetPendingNum() == 0);
    subscriber.Release();

    // 消费完一批消息后，旧唤醒提示不能被当成仍有消息。
    // A wake hint left by a drained batch must not report another message.
    TEST_ASSERT(subscriber.Wait(2) == LibXR::ErrorCode::TIMEOUT);
    std::thread next_publish(
        [&publisher]()
        {
          LibXR::Thread::Sleep(10);
          SharedData next;
          TEST_ASSERT(publisher.CreateData(next) == LibXR::ErrorCode::OK);
          FillFrame(*next.GetData(), 103);
          TEST_ASSERT(publisher.Publish(next) == LibXR::ErrorCode::OK);
        });
    const auto wait_result = subscriber.Wait(LONG_WAIT_MS);
    next_publish.join();
    TEST_ASSERT(wait_result == LibXR::ErrorCode::OK);
    AssertFrame(*subscriber.GetData(), 103);
    subscriber.Release();
    TEST_ASSERT(subscriber.Wait(2) == LibXR::ErrorCode::TIMEOUT);
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
    TEST_ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name);
    TEST_ASSERT(subscriber.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 201);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 202);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 203);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::FULL);

    TEST_ASSERT(subscriber.GetPendingNum() == 2);
    TEST_ASSERT(subscriber.GetDropNum() == 1);
    TEST_ASSERT(publisher.GetPublishFailedNum() == 1);

    SharedData recv_data;
    TEST_ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_data.GetSequence() == 1);
    AssertFrame(*recv_data.GetData(), 201);
    const SharedData& const_recv_data = recv_data;
    const_recv_data.GetData()->seq = 1201;
    TEST_ASSERT(const_recv_data.GetData()->seq == 1201);
    recv_data.Reset();

    TEST_ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_data.GetSequence() == 2);
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
    TEST_ASSERT(publisher.Valid());

    SharedSubscriber subscriber(topic_name,
                                LibXR::LinuxSharedSubscriberMode::BROADCAST_DROP_OLD);
    TEST_ASSERT(subscriber.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 211);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 212);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 213);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    TEST_ASSERT(subscriber.GetPendingNum() == 2);
    TEST_ASSERT(subscriber.GetDropNum() == 1);
    TEST_ASSERT(publisher.GetPublishFailedNum() == 0);

    SharedData recv_data;
    TEST_ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_data.GetSequence() == 2);
    AssertFrame(*recv_data.GetData(), 212);
    recv_data.Reset();

    TEST_ASSERT(subscriber.Wait(recv_data, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_data.GetSequence() == 3);
    AssertFrame(*recv_data.GetData(), 213);
    recv_data.Reset();
  }
}
}  // namespace LinuxShmTopicTest
