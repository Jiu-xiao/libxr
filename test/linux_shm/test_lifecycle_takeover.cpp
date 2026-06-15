/**
 * @file test_lifecycle_takeover.cpp
 * @brief `LinuxSharedTopic` stale publisher takeover 子验证。 Split verification unit for stale-publisher takeover semantics.
 * @details 测试项目：
 *          1. 旧 publisher 退出后新的 creator 能重新接管共享段。
 *          Test items:
 *          1. A fresh creator can take over the shared segment after the old publisher exits.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{

void RunLifecycleTakeoverScenario()
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
}

}  // namespace LinuxShmTopicTest
