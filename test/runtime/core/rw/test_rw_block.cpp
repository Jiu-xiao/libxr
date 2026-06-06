/**
 * @file test_rw_block.cpp
 * @brief runtime `rw` 阻塞/超时场景聚合入口。 Aggregation entry for split runtime `rw` blocking/timeout scenarios.
 * @details 测试项目：
 *          1. 聚合阻塞 `Stream` 子场景。
 *          2. 聚合超时、零长度与队列补齐子场景。
 *          3. 聚合 `FailAndClearAll` 阻塞等待者子场景。
 *          4. 聚合阻塞等待者生命周期子场景。
 *          Test items:
 *          1. Aggregate blocking `Stream` sub-scenarios.
 *          2. Aggregate timeout, zero-length, and queue-completion sub-scenarios.
 *          3. Aggregate `FailAndClearAll` blocking-waiter sub-scenarios.
 *          4. Aggregate blocking waiter lifecycle sub-scenarios.
 */
#include "rw_runtime_test_common.hpp"

void RunRuntimeRwBlockStreamTests();
void RunRuntimeRwBlockTimeoutTests();
void RunRuntimeRwBlockFailClearTests();
void RunRuntimeRwBlockWaiterTests();

/**
 * @brief 测试项函数 `RunRuntimeRwBlockTests`。 Test-item function `RunRuntimeRwBlockTests`.
 * @details 测试内容：执行拆分后的 runtime `rw` 阻塞/超时子场景。 Execute the split runtime `rw` blocking/timeout sub-scenarios.
 *          测试原理：保留原有聚合入口，同时让不同状态语义落到更小的主题文件里。 Preserve the original aggregation entry while moving different state semantics into smaller themed files.
 */
void RunRuntimeRwBlockTests()
{
  RunRuntimeRwBlockStreamTests();
  RunRuntimeRwBlockTimeoutTests();
  RunRuntimeRwBlockFailClearTests();
  RunRuntimeRwBlockWaiterTests();
}
