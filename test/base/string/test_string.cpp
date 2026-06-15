/**
 * @file test_string.cpp
 * @brief 运行时字符串测试聚合入口。 Aggregation entry for split runtime-string tests.
 * @details 测试项目：
 *          1. 聚合文本构造子场景。
 *          2. 聚合空指针错误路径子场景。
 *          3. 聚合重格式化子场景。
 *          Test items:
 *          1. Aggregate text-construction sub-scenarios.
 *          2. Aggregate null-pointer error-path sub-scenarios.
 *          3. Aggregate reformat/reprintf sub-scenarios.
 */
#include "string_test_common.hpp"

void RunRuntimeStringTextTests();
void RunRuntimeStringErrorTests();
void RunRuntimeStringFormatTests();

/**
 * @brief 测试入口函数 `test_string`。 Test entry function `test_string`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_string()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunRuntimeStringTextTests();
  RunRuntimeStringErrorTests();
  RunRuntimeStringFormatTests();
}
