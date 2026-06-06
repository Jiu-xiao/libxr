/**
 * @file test_lifecycle.cpp
 * @brief `LinuxSharedTopic` 生命周期语义子验证。 Split verification unit for `LinuxSharedTopic` lifecycle semantics.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
/**
 * @brief 测试项函数 `RunLifecycleScenarios`。 Test-item function `RunLifecycleScenarios`.
 * @details 测试内容：验证 stale publisher takeover、slot 引用钉住，以及死亡 subscriber 的回收。 Execute stale-publisher takeover, slot-reference pinning, and dead-subscriber reclamation scenarios.
 *          测试原理：这些场景都在验证共享内存对象生命周期和引用回收，而不是负载传输内容本身。 These scenarios all validate shared-memory object lifecycle and reference reclamation rather than payload transport itself.
 */
void RunLifecycleScenarios()
{
  char topic_name[96] = {};
  // Stale publisher takeover should allow a fresh creator to reopen the segment.
  UNUSED(SharedTopic::Remove(topic_name));
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_takeover");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 4;
    config.subscriber_num = 1;
    config.queue_num = 4;

    pid_t child = fork();
    ASSERT(child >= 0);

    if (child == 0)
    {
      SharedTopic publisher(topic_name, config);
      if (!publisher.Valid())
      {
        _exit(20);
      }

      IPCFrame frame = {};
      FillFrame(frame, 150);
      if (publisher.Publish(frame) != LibXR::ErrorCode::OK)
      {
        _exit(21);
      }

      _exit(0);
    }

    ExpectChildExit(child);

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 151);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
  }
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
  // A dead subscriber should be reclaimed together with its held slot state.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_dead_sub");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 2;
    config.subscriber_num = 1;
    config.queue_num = 4;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    int ack_pipe[2] = {-1, -1};
    int cmd_pipe[2] = {-1, -1};
    ASSERT(pipe(ack_pipe) == 0);
    ASSERT(pipe(cmd_pipe) == 0);

    pid_t child = fork();
    ASSERT(child >= 0);

    if (child == 0)
    {
      close(ack_pipe[0]);
      close(cmd_pipe[1]);

      SharedSubscriber subscriber(topic_name);
      if (!subscriber.Valid())
      {
        _exit(10);
      }

      SharedData recv_data;
      if (subscriber.Wait(recv_data, LONG_WAIT_MS) != LibXR::ErrorCode::OK)
      {
        _exit(11);
      }

      uint8_t ack = 0xA5;
      if (write(ack_pipe[1], &ack, sizeof(ack)) != static_cast<ssize_t>(sizeof(ack)))
      {
        _exit(12);
      }

      uint8_t cmd = 0;
      if (read(cmd_pipe[0], &cmd, sizeof(cmd)) != static_cast<ssize_t>(sizeof(cmd)))
      {
        _exit(13);
      }

      _exit(0);
    }

    close(ack_pipe[1]);
    close(cmd_pipe[0]);

    WaitForSubscriberNum(publisher, 1);

    IPCFrame frame = {};
    FillFrame(frame, 401);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    uint8_t ack = 0;
    ASSERT(read(ack_pipe[0], &ack, sizeof(ack)) == static_cast<ssize_t>(sizeof(ack)));

    FillFrame(frame, 402);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    uint8_t cmd = 0x5A;
    ASSERT(write(cmd_pipe[1], &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd)));

    ExpectChildExit(child);
    ASSERT(publisher.GetSubscriberNum() == 1);

    SharedData data0;
    SharedData data1;
    SharedData data2;
    ASSERT(publisher.CreateData(data0) == LibXR::ErrorCode::OK);
    ASSERT(publisher.GetSubscriberNum() == 0);
    ASSERT(publisher.CreateData(data1) == LibXR::ErrorCode::OK);
    ASSERT(publisher.CreateData(data2) == LibXR::ErrorCode::FULL);

    close(ack_pipe[0]);
    close(cmd_pipe[1]);
  }
}
}  // namespace LinuxShmTopicTest
