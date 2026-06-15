/**
 * @file test_input.cpp
 * @brief `Terminal` 输入测试聚合入口。 Aggregation entry for split `Terminal` input tests.
 * @details 测试项目：
 *          1. 聚合 CRLF 抑制与历史导航子场景。
 *          2. 聚合行内编辑子场景。
 *          Test items:
 *          1. Aggregate CRLF-suppression and history-navigation sub-scenarios.
 *          2. Aggregate inline-edit sub-scenarios.
 */
#include "terminal_session_test_common.hpp"

void RunTerminalInputHistoryTests();
void RunTerminalInputEditTests();

/**
 * @brief 测试入口函数 `test_terminal_input`。 Test entry function `test_terminal_input`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_terminal_input()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunTerminalInputHistoryTests();
  RunTerminalInputEditTests();
}
