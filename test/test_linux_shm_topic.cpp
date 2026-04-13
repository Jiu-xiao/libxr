#include <array>
#include <cstdio>
#include <cstdlib>

#include <sys/wait.h>
#include <unistd.h>

#include "libxr.hpp"
#include "test.hpp"

namespace
{

struct IPCFrame
{
  uint32_t seq = 0;
  uint32_t checksum = 0;
  std::array<uint8_t, 128> payload = {};
};

uint32_t ComputeChecksum(const IPCFrame& frame)
{
  uint32_t sum = frame.seq;
  for (uint8_t byte : frame.payload)
  {
    sum = sum * 131U + byte;
  }
  return sum;
}

void FillFrame(IPCFrame& frame, uint32_t seq)
{
  frame.seq = seq;
  for (size_t i = 0; i < frame.payload.size(); ++i)
  {
    frame.payload[i] = static_cast<uint8_t>((seq + i * 3U) & 0xFFU);
  }
  frame.checksum = ComputeChecksum(frame);
}

void AssertFrame(const IPCFrame& frame, uint32_t expected_seq)
{
  ASSERT(frame.seq == expected_seq);
  ASSERT(frame.checksum == ComputeChecksum(frame));
}

}  // namespace

void test_linux_shm_topic()
{
  using SharedTopic = LibXR::LinuxSharedTopic<IPCFrame>;
  using SharedData = SharedTopic::Data;
  using SharedSubscriber = SharedTopic::SyncSubscriber;

  char topic_name[96] = {};

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_local_%d",
                static_cast<int>(getpid()));
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
    ASSERT(attach_only.CreateData(attach_data) == ErrorCode::STATE_ERR);

    SharedData data0;
    ASSERT(publisher.CreateData(data0) == ErrorCode::OK);
    FillFrame(*data0.GetData(), 100);
    ASSERT(publisher.Publish(data0) == ErrorCode::OK);

    SharedData data1;
    ASSERT(publisher.CreateData(data1) == ErrorCode::OK);
    FillFrame(*data1.GetData(), 101);
    ASSERT(publisher.Publish(data1) == ErrorCode::OK);

    SharedData data2;
    ASSERT(publisher.CreateData(data2) == ErrorCode::FULL);

    ASSERT(subscriber.Wait(100) == ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 100);
    ASSERT(subscriber.GetPendingNum() == 1);
    subscriber.Release();

    ASSERT(publisher.CreateData(data2) == ErrorCode::OK);
    FillFrame(*data2.GetData(), 102);
    ASSERT(publisher.Publish(data2) == ErrorCode::OK);

    ASSERT(subscriber.Wait(100) == ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 101);
    subscriber.Release();

    ASSERT(subscriber.Wait(100) == ErrorCode::OK);
    ASSERT(subscriber.GetData() != nullptr);
    AssertFrame(*subscriber.GetData(), 102);
    ASSERT(subscriber.GetPendingNum() == 0);
    subscriber.Release();
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_takeover_%d",
                static_cast<int>(getpid()));
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
      if (publisher.Publish(frame) != ErrorCode::OK)
      {
        _exit(21);
      }

      _exit(0);
    }

    int status = 0;
    ASSERT(waitpid(child, &status, 0) == child);
    ASSERT(WIFEXITED(status));
    ASSERT(WEXITSTATUS(status) == 0);

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 151);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_queue_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 202);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 203);
    ASSERT(publisher.Publish(frame) == ErrorCode::FULL);

    ASSERT(subscriber.GetPendingNum() == 2);
    ASSERT(subscriber.GetDropNum() == 1);
    ASSERT(publisher.GetPublishFailedNum() == 1);

    SharedData recv_data;
    ASSERT(subscriber.Wait(recv_data, 100) == ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 1);
    AssertFrame(*recv_data.GetData(), 201);
    recv_data.Reset();

    ASSERT(subscriber.Wait(recv_data, 100) == ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 2);
    AssertFrame(*recv_data.GetData(), 202);
    recv_data.Reset();
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_drop_old_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 212);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 213);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    ASSERT(subscriber.GetPendingNum() == 2);
    ASSERT(subscriber.GetDropNum() == 1);
    ASSERT(publisher.GetPublishFailedNum() == 0);

    SharedData recv_data;
    ASSERT(subscriber.Wait(recv_data, 100) == ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 2);
    AssertFrame(*recv_data.GetData(), 212);
    recv_data.Reset();

    ASSERT(subscriber.Wait(recv_data, 100) == ErrorCode::OK);
    ASSERT(recv_data.GetSequence() == 3);
    AssertFrame(*recv_data.GetData(), 213);
    recv_data.Reset();
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_ref_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    ASSERT(subscriber_a.Wait(100) == ErrorCode::OK);
    ASSERT(subscriber_b.Wait(100) == ErrorCode::OK);
    AssertFrame(*subscriber_a.GetData(), 301);
    AssertFrame(*subscriber_b.GetData(), 301);

    subscriber_a.Release();

    SharedData blocked_data;
    ASSERT(publisher.CreateData(blocked_data) == ErrorCode::FULL);

    subscriber_b.Release();
    ASSERT(publisher.CreateData(blocked_data) == ErrorCode::OK);
    FillFrame(*blocked_data.GetData(), 302);
    ASSERT(publisher.Publish(blocked_data) == ErrorCode::OK);

    ASSERT(subscriber_a.Wait(100) == ErrorCode::OK);
    ASSERT(subscriber_b.Wait(100) == ErrorCode::OK);
    AssertFrame(*subscriber_a.GetData(), 302);
    AssertFrame(*subscriber_b.GetData(), 302);
    subscriber_a.Release();
    subscriber_b.Release();
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_bal_rr_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 352);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 353);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 354);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    SharedData recv_a1;
    SharedData recv_a2;
    SharedData recv_b1;
    SharedData recv_b2;

    ASSERT(subscriber_a.Wait(recv_a1, 100) == ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b1, 100) == ErrorCode::OK);
    ASSERT(subscriber_a.Wait(recv_a2, 100) == ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b2, 100) == ErrorCode::OK);

    ASSERT(recv_a1.GetData()->seq == 351);
    ASSERT(recv_b1.GetData()->seq == 352);
    ASSERT(recv_a2.GetData()->seq == 353);
    ASSERT(recv_b2.GetData()->seq == 354);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_bal_rr_dead_%d",
                static_cast<int>(getpid()));
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
      if (subscriber.Wait(data, 2000) != ErrorCode::OK)
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

    SharedSubscriber subscriber_alive(topic_name, LibXR::LinuxSharedSubscriberMode::BALANCE_RR);
    ASSERT(subscriber_alive.Valid());

    IPCFrame frame = {};
    FillFrame(frame, 361);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    uint32_t first_seq = 0;
    ASSERT(read(ack_pipe[0], &first_seq, sizeof(first_seq)) ==
           static_cast<ssize_t>(sizeof(first_seq)));
    ASSERT(first_seq == 361);

    uint8_t cmd = 0xA5;
    ASSERT(write(cmd_pipe[1], &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd)));

    int status = 0;
    ASSERT(waitpid(child, &status, 0) == child);
    ASSERT(WIFEXITED(status));
    ASSERT(WEXITSTATUS(status) == 0);

    FillFrame(frame, 362);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 363);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    SharedData recv_alive1;
    SharedData recv_alive2;
    ASSERT(subscriber_alive.Wait(recv_alive1, 2000) == ErrorCode::OK);
    ASSERT(subscriber_alive.Wait(recv_alive2, 2000) == ErrorCode::OK);
    ASSERT(recv_alive1.GetData()->seq == 362);
    ASSERT(recv_alive2.GetData()->seq == 363);

    close(ack_pipe[0]);
    close(cmd_pipe[1]);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_bal_rr_skip_full_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 372);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 373);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    SharedData recv_b0;
    ASSERT(subscriber_b.Wait(recv_b0, 100) == ErrorCode::OK);
    ASSERT(recv_b0.GetData()->seq == 372);
    recv_b0.Reset();

    FillFrame(frame, 374);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    SharedData recv_a;
    SharedData recv_b1;
    SharedData recv_c;
    ASSERT(subscriber_a.Wait(recv_a, 100) == ErrorCode::OK);
    ASSERT(subscriber_b.Wait(recv_b1, 100) == ErrorCode::OK);
    ASSERT(subscriber_c.Wait(recv_c, 100) == ErrorCode::OK);

    ASSERT(recv_a.GetData()->seq == 371);
    ASSERT(recv_b1.GetData()->seq == 374);
    ASSERT(recv_c.GetData()->seq == 373);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_mixed_modes_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 382);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);
    FillFrame(frame, 383);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    SharedData bc0;
    SharedData bc1;
    SharedData bc2;
    ASSERT(subscriber_broadcast.Wait(bc0, 100) == ErrorCode::OK);
    ASSERT(subscriber_broadcast.Wait(bc1, 100) == ErrorCode::OK);
    ASSERT(subscriber_broadcast.Wait(bc2, 100) == ErrorCode::OK);
    ASSERT(bc0.GetData()->seq == 381);
    ASSERT(bc1.GetData()->seq == 382);
    ASSERT(bc2.GetData()->seq == 383);

    SharedData rr_a0;
    SharedData rr_b0;
    SharedData rr_a1;
    ASSERT(subscriber_rr_a.Wait(rr_a0, 100) == ErrorCode::OK);
    ASSERT(subscriber_rr_b.Wait(rr_b0, 100) == ErrorCode::OK);
    ASSERT(subscriber_rr_a.Wait(rr_a1, 100) == ErrorCode::OK);
    ASSERT(rr_a0.GetData()->seq == 381);
    ASSERT(rr_b0.GetData()->seq == 382);
    ASSERT(rr_a1.GetData()->seq == 383);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_rr_group_required_%d",
                static_cast<int>(getpid()));
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
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    FillFrame(frame, 392);
    ASSERT(publisher.Publish(frame) == ErrorCode::FULL);

    SharedData bc0;
    ASSERT(subscriber_broadcast.Wait(bc0, 100) == ErrorCode::OK);
    ASSERT(bc0.GetData()->seq == 391);

    SharedData rr0;
    ASSERT(subscriber_rr.Wait(rr0, 100) == ErrorCode::OK);
    ASSERT(rr0.GetData()->seq == 391);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_dead_sub_%d",
                static_cast<int>(getpid()));
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
      if (subscriber.Wait(recv_data, 2000) != ErrorCode::OK)
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

    for (int retry = 0; retry < 200 && publisher.GetSubscriberNum() == 0; ++retry)
    {
      usleep(10000);
    }
    ASSERT(publisher.GetSubscriberNum() == 1);

    IPCFrame frame = {};
    FillFrame(frame, 401);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    uint8_t ack = 0;
    ASSERT(read(ack_pipe[0], &ack, sizeof(ack)) == static_cast<ssize_t>(sizeof(ack)));

    FillFrame(frame, 402);
    ASSERT(publisher.Publish(frame) == ErrorCode::OK);

    uint8_t cmd = 0x5A;
    ASSERT(write(cmd_pipe[1], &cmd, sizeof(cmd)) == static_cast<ssize_t>(sizeof(cmd)));

    int status = 0;
    ASSERT(waitpid(child, &status, 0) == child);
    ASSERT(WIFEXITED(status));
    ASSERT(WEXITSTATUS(status) == 0);
    ASSERT(publisher.GetSubscriberNum() == 0);

    SharedData data0;
    SharedData data1;
    SharedData data2;
    ASSERT(publisher.CreateData(data0) == ErrorCode::OK);
    ASSERT(publisher.CreateData(data1) == ErrorCode::OK);
    ASSERT(publisher.CreateData(data2) == ErrorCode::FULL);

    close(ack_pipe[0]);
    close(cmd_pipe[1]);
  }

  UNUSED(SharedTopic::Remove(topic_name));

  std::snprintf(topic_name, sizeof(topic_name), "linux_shm_fork_%d",
                static_cast<int>(getpid()));
  UNUSED(SharedTopic::Remove(topic_name));

  {
    LibXR::LinuxSharedTopicConfig config;
    config.slot_num = 64;
    config.subscriber_num = 4;
    config.queue_num = 64;

    SharedTopic publisher(topic_name, config);
    ASSERT(publisher.Valid());

    pid_t child = fork();
    ASSERT(child >= 0);

    if (child == 0)
    {
      SharedSubscriber subscriber(topic_name);
      if (!subscriber.Valid())
      {
        _exit(2);
      }

      for (uint32_t seq = 1; seq <= 32; ++seq)
      {
        SharedData recv_data;
        if (subscriber.Wait(recv_data, 2000) != ErrorCode::OK)
        {
          _exit(3);
        }

        const IPCFrame* frame = recv_data.GetData();
        if (frame == nullptr)
        {
          _exit(4);
        }

        if (recv_data.GetSequence() != seq || frame->seq != seq ||
            frame->checksum != ComputeChecksum(*frame))
        {
          _exit(5);
        }
      }

      _exit(0);
    }

    for (int retry = 0; retry < 200 && publisher.GetSubscriberNum() == 0; ++retry)
    {
      usleep(10000);
    }
    ASSERT(publisher.GetSubscriberNum() == 1);

    for (uint32_t seq = 1; seq <= 32; ++seq)
    {
      SharedData data;
      ASSERT(publisher.CreateData(data) == ErrorCode::OK);
      FillFrame(*data.GetData(), seq);
      ASSERT(publisher.Publish(data) == ErrorCode::OK);
    }

    int status = 0;
    ASSERT(waitpid(child, &status, 0) == child);
    ASSERT(WIFEXITED(status));
    ASSERT(WEXITSTATUS(status) == 0);
  }

  UNUSED(SharedTopic::Remove(topic_name));
}
