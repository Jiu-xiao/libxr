/**
 * @file test_cross_process.cpp
 * @brief `LinuxSharedTopic` 纯跨进程顺序子验证。 Split verification unit for pure cross-process ordering semantics.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
/**
 * @brief 测试项函数 `RunCrossProcessScenarios`。 Test-item function `RunCrossProcessScenarios`.
 * @details 测试内容：验证独立 publisher/subscriber 进程之间的顺序和 payload 完整性。 Execute the cross-process publish/subscribe ordering and payload-integrity scenario.
 *          测试原理：单独把纯跨进程序列完整性拉出来，避免它被其他资源管理场景噪声掩盖。 Isolate the pure cross-process ordering/integrity check so it is not obscured by unrelated resource-management scenarios.
 */
void RunCrossProcessScenarios()
{
  char topic_name[96] = {};
  // Cross-process publish/subscribe should preserve ordering and payload integrity.
  MakeTopicName(topic_name, sizeof(topic_name), "linux_shm_fork");
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
        if (subscriber.Wait(recv_data, LONG_WAIT_MS) != LibXR::ErrorCode::OK)
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

    WaitForSubscriberNum(publisher, 1);

    for (uint32_t seq = 1; seq <= 32; ++seq)
    {
      SharedData data;
      ASSERT(publisher.CreateData(data) == LibXR::ErrorCode::OK);
      FillFrame(*data.GetData(), seq);
      ASSERT(publisher.Publish(data) == LibXR::ErrorCode::OK);
    }

    ExpectChildExit(child);
  }
}
}  // namespace LinuxShmTopicTest
