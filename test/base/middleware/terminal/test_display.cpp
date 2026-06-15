/**
 * @file test_display.cpp
 * @brief `Terminal` 显示测试聚合入口。 Aggregation entry for split `Terminal` display tests.
 * @details 测试项目：
 *          1. 聚合换行/清屏显示子场景。
 *          2. 聚合历史显示与行内重绘子场景。
 *          Test items:
 *          1. Aggregate line-feed/clear-sequence display sub-scenarios.
 *          2. Aggregate history-display and inline-redraw sub-scenarios.
 */
#include "terminal_display_test_common.hpp"

void RunTerminalDisplayModeTests();
void RunTerminalDisplayHistoryTests();

/**
 * @brief 测试入口函数 `test_terminal_display`。 Test entry function `test_terminal_display`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_terminal_display()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunTerminalDisplayModeTests();
  RunTerminalDisplayHistoryTests();
}
