/**
 * @file test_rw_block.cpp
 * @brief base `rw` 阻塞/超时场景聚合入口。 Aggregation entry for split base `rw` blocking/timeout scenarios.
 * @details 测试项目：
 *          1. 聚合阻塞 `Stream` 子场景。
 *          2. 聚合超时与立即错误子场景。
 *          3. 聚合阻塞等待者生命周期子场景。
 *          Test items:
 *          1. Aggregate blocking `Stream` sub-scenarios.
 *          2. Aggregate timeout and immediate-error sub-scenarios.
 *          3. Aggregate blocking waiter lifecycle sub-scenarios.
 */
#include "rw_test_common.hpp"

void RunBaseRwBlockStreamTests();
void RunBaseRwBlockTimeoutTests();
void RunBaseRwBlockWaiterTests();

/**
 * @brief 测试项函数 `RunBaseRwBlockTests`。 Test-item function `RunBaseRwBlockTests`.
 * @details 测试内容：执行拆分后的 base `rw` 阻塞/超时子场景。 Execute the split base `rw` blocking/timeout sub-scenarios.
 *          测试原理：保留原有聚合入口，同时让具体场景落到更小的主题文件里。 Preserve the original aggregation entry while moving concrete scenarios into smaller themed files.
 */
void RunBaseRwBlockTests()
{
  RunBaseRwBlockStreamTests();
  RunBaseRwBlockTimeoutTests();
  RunBaseRwBlockWaiterTests();
}
