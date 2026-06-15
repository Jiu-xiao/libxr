/**
 * @file test_balance_rr.cpp
 * @brief `LinuxSharedTopic` BALANCE_RR 策略聚合入口。 Aggregation entry for split `LinuxSharedTopic` BALANCE_RR semantics.
 * @details 测试项目：
 *          1. 聚合轮转分发子场景。
 *          2. 聚合死亡成员回收子场景。
 *          3. 聚合跳过满队列成员子场景。
 *          Test items:
 *          1. Aggregate rotation sub-scenarios.
 *          2. Aggregate dead-member reclamation sub-scenarios.
 *          3. Aggregate full-member skipping sub-scenarios.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
void RunBalanceRoundRobinRotateScenario();
void RunBalanceRoundRobinDeadMemberScenario();
void RunBalanceRoundRobinSkipFullScenario();

void RunBalanceRoundRobinScenarios()
{
  RunBalanceRoundRobinRotateScenario();
  RunBalanceRoundRobinDeadMemberScenario();
  RunBalanceRoundRobinSkipFullScenario();
}
}  // namespace LinuxShmTopicTest
