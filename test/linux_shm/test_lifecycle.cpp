/**
 * @file test_lifecycle.cpp
 * @brief `LinuxSharedTopic` 生命周期语义聚合入口。 Aggregation entry for split `LinuxSharedTopic` lifecycle semantics.
 * @details 测试项目：
 *          1. 聚合 stale publisher takeover 子场景。
 *          2. 聚合 slot 引用钉住子场景。
 *          3. 聚合死亡 subscriber 回收子场景。
 *          Test items:
 *          1. Aggregate stale-publisher takeover sub-scenarios.
 *          2. Aggregate slot-reference pinning sub-scenarios.
 *          3. Aggregate dead-subscriber reclamation sub-scenarios.
 */
#include "linux_shm_topic_test_common.hpp"

namespace LinuxShmTopicTest
{
void RunLifecycleTakeoverScenario();
void RunLifecycleSlotReferenceScenario();
void RunLifecycleDeadSubscriberScenario();

void RunLifecycleScenarios()
{
  RunLifecycleTakeoverScenario();
  RunLifecycleSlotReferenceScenario();
  RunLifecycleDeadSubscriberScenario();
}
}  // namespace LinuxShmTopicTest
