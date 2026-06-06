/**
 * @file test_linux_shm_topic.cpp
 * @brief `LinuxSharedTopic` 环境验证聚合入口。 Aggregation entry for split `LinuxSharedTopic` environment verification.
 */
#include "linux_shm_topic_test_common.hpp"

/**
 * @brief 测试入口函数 `test_linux_shm_topic`。 Test entry function `test_linux_shm_topic`.
 * @details 测试内容：按分组顺序执行拆分后的 `LinuxSharedTopic` 环境验证场景。 Execute the split `LinuxSharedTopic` environment verification scenarios in grouped order.
 *          测试原理：把大而杂的环境验证文件收成多个语义子场景，再由单一入口统一调度。 Replace one oversized environment-verification file with multiple semantic sub-scenarios and dispatch them from one entry.
 */
void test_linux_shm_topic()
{
  LinuxShmTopicTest::RunAttachQueueScenarios();
  LinuxShmTopicTest::RunLifecycleScenarios();
  LinuxShmTopicTest::RunBalanceRoundRobinScenarios();
  LinuxShmTopicTest::RunMixedModeScenarios();
  LinuxShmTopicTest::RunCrossProcessScenarios();
}
