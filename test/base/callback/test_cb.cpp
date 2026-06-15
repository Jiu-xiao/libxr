/**
 * @file test_cb.cpp
 * @brief `LibXR::Callback` 测试聚合入口。 Aggregation entry for split `LibXR::Callback` tests.
 * @details 测试项目：
 *          1. 聚合空回调与直接重入子场景。
 *          2. 聚合 guarded 与 lambda 绑定子场景。
 *          Test items:
 *          1. Aggregate empty-callback and direct-reentry sub-scenarios.
 *          2. Aggregate guarded and lambda-binding sub-scenarios.
 */
#include "cb_test_common.hpp"

void RunDirectCallbackTests();
void RunGuardedCallbackTests();

/**
 * @brief 测试入口函数 `test_cb`。 Test entry function `test_cb`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_cb()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunDirectCallbackTests();
  RunGuardedCallbackTests();
}
