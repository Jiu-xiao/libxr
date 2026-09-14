/**
 * @file test_balance_rr.cpp
 * @brief 共享 Topic 的轮询分发测试 / Shared Topic round-robin delivery tests.
 *
 * 检查 LinuxSharedTopic 轮流投递、跳过满队列及回收退出的订阅者。
 * Check LinuxSharedTopic rotation, skipping full queues and reclaiming exited
 * subscribers.
 */

#include "linux_shm_topic_test_common.hpp"
#include "test_assert.hpp"

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
    TEST_ASSERT(publisher.Valid());

    SharedSubscriber subscriber_a(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_b(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    TEST_ASSERT(subscriber_a.Valid());
    TEST_ASSERT(subscriber_b.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 351);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 352);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 353);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 354);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_a1;
    SharedData recv_a2;
    SharedData recv_b1;
    SharedData recv_b2;

    TEST_ASSERT(subscriber_a.Wait(recv_a1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_b.Wait(recv_b1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_a.Wait(recv_a2, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_b.Wait(recv_b2, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);

    TEST_ASSERT(recv_a1.GetData()->seq == 351);
    TEST_ASSERT(recv_b1.GetData()->seq == 352);
    TEST_ASSERT(recv_a2.GetData()->seq == 353);
    TEST_ASSERT(recv_b2.GetData()->seq == 354);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

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
    TEST_ASSERT(publisher.Valid());

    SharedSubscriber subscriber_a(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_b(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    SharedSubscriber subscriber_c(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    TEST_ASSERT(subscriber_a.Valid());
    TEST_ASSERT(subscriber_b.Valid());
    TEST_ASSERT(subscriber_c.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 371);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 372);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 373);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_b0;
    TEST_ASSERT(subscriber_b.Wait(recv_b0, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_b0.GetData()->seq == 372);
    recv_b0.Reset();

    FillFrame(frame, 374);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_a;
    SharedData recv_b1;
    SharedData recv_c;
    TEST_ASSERT(subscriber_a.Wait(recv_a, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_b.Wait(recv_b1, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_c.Wait(recv_c, SHORT_WAIT_MS) == LibXR::ErrorCode::OK);

    TEST_ASSERT(recv_a.GetData()->seq == 371);
    TEST_ASSERT(recv_b1.GetData()->seq == 374);
    TEST_ASSERT(recv_c.GetData()->seq == 373);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

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
    TEST_ASSERT(publisher.Valid());

    int ack_pipe[2] = {-1, -1};
    int cmd_pipe[2] = {-1, -1};
    TEST_ASSERT(pipe(ack_pipe) == 0);
    TEST_ASSERT(pipe(cmd_pipe) == 0);

    pid_t child = fork();
    TEST_ASSERT(child >= 0);

    if (child == 0)
    {
      close(ack_pipe[0]);
      close(cmd_pipe[1]);

      SharedSubscriber subscriber(topic_name,
                                  LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
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
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    uint32_t first_seq = 0;
    TEST_ASSERT(read(ack_pipe[0], &first_seq, sizeof(first_seq)) ==
                static_cast<ssize_t>(sizeof(first_seq)));
    TEST_ASSERT(first_seq == 361);

    SharedSubscriber subscriber_alive(topic_name,
                                      LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    TEST_ASSERT(subscriber_alive.Valid());
    WaitForSubscriberNum(publisher, 2);

    uint8_t cmd = 0xA5;
    TEST_ASSERT(write(cmd_pipe[1], &cmd, sizeof(cmd)) ==
                static_cast<ssize_t>(sizeof(cmd)));

    ExpectChildExit(child);

    FillFrame(frame, 362);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);
    FillFrame(frame, 363);
    TEST_ASSERT(publisher.Publish(frame) == LibXR::ErrorCode::OK);

    SharedData recv_alive1;
    SharedData recv_alive2;
    TEST_ASSERT(subscriber_alive.Wait(recv_alive1, LONG_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(subscriber_alive.Wait(recv_alive2, LONG_WAIT_MS) == LibXR::ErrorCode::OK);
    TEST_ASSERT(recv_alive1.GetData()->seq == 362);
    TEST_ASSERT(recv_alive2.GetData()->seq == 363);

    close(ack_pipe[0]);
    close(cmd_pipe[1]);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}

void RunBalanceRoundRobinScenarios()
{
  RunBalanceRoundRobinRotateScenario();
  RunBalanceRoundRobinDeadMemberScenario();
  RunBalanceRoundRobinSkipFullScenario();
}
}  // namespace LinuxShmTopicTest
