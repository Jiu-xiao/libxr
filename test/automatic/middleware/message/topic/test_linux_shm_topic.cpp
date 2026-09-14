/**
 * @file test_linux_shm_topic.cpp
 * @brief 共享 Topic 功能测试入口 / Shared Topic functional test entry.
 *
 * 汇总 LinuxSharedTopic 的连接、生命周期、分发方式和跨进程测试。
 * Run LinuxSharedTopic attachment, lifetime, delivery-mode and cross-process tests.
 */

#include "linux_shm_topic_test_common.hpp"

void test_linux_shm_topic()
{
  LinuxShmTopicTest::RunAttachQueueScenarios();
  LinuxShmTopicTest::RunLifecycleScenarios();
  LinuxShmTopicTest::RunBalanceRoundRobinScenarios();
  LinuxShmTopicTest::RunMixedModeScenarios();
  LinuxShmTopicTest::RunCrossProcessScenarios();
}
