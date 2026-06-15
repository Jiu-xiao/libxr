/**
 * @file test_balance_rr_dead.cpp
 * @brief `LinuxSharedTopic` BALANCE_RR 死亡成员回收子验证。 Split verification unit for BALANCE_RR dead-member reclamation semantics.
 * @details 测试项目：
 *          1. 死亡 balanced subscriber 被回收后组仍能继续工作。
 *          Test items:
 *          1. The group keeps working after a dead balanced subscriber is reclaimed.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunBalanceRoundRobinDeadMemberScenario()
{
  char topic_name[96] = {};
  // A dead balanced subscriber must be recycled so the group can keep running.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_bal_rr_dead");
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 8;
    config.subscriber_num = 2;
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

      SharedSubscriber subscriber(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
      if (!subscriber.Valid())
      {
        _exit(40);
      }

      SharedData data;
      if (subscriber.Wait(data, LONG_WAIT_MS) != LibXR::ErrorCode::OK)
      {
        _exit(41);
      }

      const uint32_t seq = data.GetData()->seq;
      if (write(ack_pipe[1], &seq, sizeof(seq)) != static_cast<ssize_t>(sizeof(seq)))
      {
        _exit(42);
      }

      uint8_t cmd = 0;
      if (read(cmd_pipe[0], &cmd, sizeof(cmd)) != static_cast<ssize_t>(sizeof(cmd)))
      {
        _exit(43);
      }

      _exit(0);
    }

    close(ack_pipe[1]);
    close(cmd_pipe[0]);

    WaitForSubscriberNum(publisher, 1);

    IPCFrame frame = {};
    FillFrame(frame, 361);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    uint32_t first_seq = 0;
    ASSERT(read(ack_pipe[0], &first_seq, sizeof(first_seq)) ==
           static_cast<ssize_t>(sizeof(first_seq)));
    ASSERT(first_seq == 361);

    SharedSubscriber subscriber_alive(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_alive.Valid());
    WaitForSubscriberNum(publisher, 2);

    uint8_t cmd = 0xA5;
    ASSERT(write(cmd_pipe[1], &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd)));

    ExpectChildExit(child);

    FillFrame(frame, 362);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 363);
    ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_alive1;
    SharedData recv_alive2;
    ASSERT(subscriber_alive.Wait(recv_alive1, LONG_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(subscriber_alive.Wait(recv_alive2, LONG_WAIT_MS) == LibXR::ErrorCode::OK);
    ASSERT(recv_alive1.GetData()->seq == 362);
    ASSERT(recv_alive2.GetData()->seq == 363);

    close(ack_pipe[0]);
    close(cmd_pipe[1]);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

}  // namespace LinuxShmTopicTest
