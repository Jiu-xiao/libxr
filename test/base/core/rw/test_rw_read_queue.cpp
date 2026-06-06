/**
 * @file test_rw_read_queue.cpp
 * @brief base `ReadPort` 读队列场景聚合入口。 Aggregation entry for split base `ReadPort` queue scenarios.
 * @details 测试项目：
 *          1. 聚合 `ClearQueuedData` 子场景。
 *          2. 聚合挂起完成与零长度读子场景。
 *          Test items:
 *          1. Aggregate `ClearQueuedData` sub-scenarios.
 *          2. Aggregate pending-completion and zero-length read sub-scenarios.
 */
#include "rw_test_common.hpp"

void RunBaseRwReadQueueClearTests();
void RunBaseRwReadQueuePendingTests();

/**
 * @brief 测试项函数 `RunBaseRwReadQueueTests`。 Test-item function `RunBaseRwReadQueueTests`.
 * @details 测试内容：执行拆分后的 base `ReadPort` 读队列子场景。 Execute the split base `ReadPort` queue sub-scenarios.
 *          测试原理：保留原有分组入口，同时把具体场景拆到更细的主题文件。 Preserve the original group entrypoint while moving concrete scenarios into smaller themed files.
 */
void RunBaseRwReadQueueTests()
{
  RunBaseRwReadQueueClearTests();
  RunBaseRwReadQueuePendingTests();
}
