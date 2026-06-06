/**
 * @file test_print.cpp
 * @brief `print` 测试聚合入口。 Aggregation entry for split `print` tests.
 *
 * 测试项目 / Test items:
 * 1. printf 前端语义。 `printf` frontend semantics.
 * 2. format 前端语义。 `format` frontend semantics.
 * 3. 公开 API 包装层。 Public print API wrappers.
 * 4. 失败路径前缀保留语义。 Failure-path prefix retention.
 */
#include "print_test_common.hpp"

/**
 * @brief 测试入口函数 `test_print`。 Test entry function `test_print`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_print()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXRPrintTest::TestPrintfFrontendSemantics();
  LibXRPrintTest::TestFormatFrontendSemantics();
  LibXRPrintTest::TestPrintApiWrappers();
  LibXRPrintTest::TestStreamBackedPrintFailureKeepsPrefix();
}
