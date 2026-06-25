/**
 * @file test_rw_fail_clear.cpp
 * @brief base `FailAndClearAll` 场景聚合入口。 Aggregation entry for split base `FailAndClearAll` scenarios.
 * @details 测试项目：
 *          1. 聚合异步完成子场景。
 *          2. 聚合队列清理与 stream 锁保持子场景。
 *          3. 阻塞等待者子场景由 runtime `rw` 入口覆盖。
 *          Test items:
 *          1. Aggregate asynchronous completion sub-scenarios.
 *          2. Aggregate queue-cleanup and stream-lock preservation sub-scenarios.
 *          3. Runtime `rw` covers blocking-waiter sub-scenarios.
 */
#include "rw_test_common.hpp"

void RunBaseRwFailAndClearAsyncTests();
void RunBaseRwFailAndClearStreamTests();

/**
 * @brief 测试项函数 `RunBaseRwFailAndClearTests`。 Test-item function `RunBaseRwFailAndClearTests`.
 * @details 测试内容：执行拆分后的 base `FailAndClearAll` 子场景。 Execute the split base `FailAndClearAll` sub-scenarios.
 *          测试原理：保留原有聚合入口，同时把不同状态语义拆到更细的主题文件。 Preserve the original aggregation entry while moving different state semantics into smaller themed files.
 */
void RunBaseRwFailAndClearTests()
{
  RunBaseRwFailAndClearAsyncTests();
  RunBaseRwFailAndClearStreamTests();
}
