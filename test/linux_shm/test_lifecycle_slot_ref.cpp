/**
 * @file test_lifecycle_slot_ref.cpp
 * @brief `LinuxSharedTopic` slot 引用钉住子验证。 Split verification unit for slot-reference pinning semantics.
 * @details 测试项目：
 *          1. slot 在所有 subscriber 释放前保持占用。
 *          2. 最后一个 subscriber 释放后 publisher 能再次创建新数据。
 *          Test items:
 *          1. A slot stays occupied until every subscriber releases it.
 *          2. After the last release, the publisher can create new data again.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunLifecycleSlotReferenceScenario()
{
  char topic_name[96] = {};
  // A slot must stay pinned until every subscriber releases it.
  UNUSED(SharedTopic::Remove(topic_name));
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_ref");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 1;
    config.subscriber_num = 2;
    config.queue_num = 4;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    SharedSubscriber subscriber_a(topic_name);
    SharedSubscriber subscriber_b(topic_name);
    ASSERT(subscriber_a.Valid());
    ASSERT(subscriber_b.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 301);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    ASSERT(subscriber_a.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    AssertFrame(*subscriber_a.GetData(), 301);
    AssertFrame(*subscriber_b.GetData(), 301);

    subscriber_a.Release();

    SharedData blocked_data;
    ASSERT(publisher.CreateData(blocked_data) == LibXR::ErrorCode::FULL);

    subscriber_b.Release();
    ASSERT(publisher.CreateData(blocked_data) == LibXR::ErrorCode::OK);
    FillFrame(*blocked_data.GetData(), 302);
    ASSERT(publisher.Publish(blocked_data) == LibXR::ErrorCode::OK);

    ASSERT(subscriber_a.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_b.Wait(SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    AssertFrame(*subscriber_a.GetData(), 302);
    AssertFrame(*subscriber_b.GetData(), 302);
    subscriber_a.Release();
    subscriber_b.Release();
  }
}

}  // namespace LinuxShmTopicTest
