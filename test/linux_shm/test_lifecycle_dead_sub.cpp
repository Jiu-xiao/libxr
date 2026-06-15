/**
 * @file test_lifecycle_dead_sub.cpp
 * @brief `LinuxSharedTopic` 死亡 subscriber 回收子验证。 Split verification unit for dead-subscriber reclamation semantics.
 * @details 测试项目：
 *          1. 死亡 subscriber 与其持有的 slot 状态会被回收。
 *          2. 回收后 publisher 的 subscriber 计数和 slot 可用性恢复。
 *          Test items:
 *          1. A dead subscriber and its held slot state are reclaimed.
 *          2. After reclamation, publisher subscriber count and slot availability recover.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunLifecycleDeadSubscriberScenario()
{
  char topic_name[96] = {};
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
