/**
 * @file test_sequential.cpp
 * @brief linux file-backed `DatabaseRawSequential` 测试聚合入口。 Aggregation entry for split linux file-backed `DatabaseRawSequential` tests.
 * @details 测试项目：
 *          1. 聚合 smoke/save 子场景。
 *          2. 聚合 fatal 失败路径子场景。
 *          Test items:
 *          1. Aggregate smoke/save sub-scenarios.
 *          2. Aggregate fatal failure-path sub-scenarios.
 */
#include "linux_database_test_common.hpp"

void RunLinuxDatabaseSequentialSmokeTests();
void RunLinuxDatabaseSequentialFailureTests();

/**
 * @brief 测试入口函数 `test_linux_database_sequential`。 Test entry function `test_linux_database_sequential`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_linux_database_sequential()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunLinuxDatabaseSequentialSmokeTests();
  RunLinuxDatabaseSequentialFailureTests();
}
