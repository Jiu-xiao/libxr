/**
 * @file test_command.cpp
 * @brief `Terminal` 内建命令测试聚合入口。 Aggregation entry for split `Terminal` builtin-command tests.
 * @details 测试项目：
 *          1. 聚合 `cd` 内建命令子场景。
 *          2. 聚合 `ls` 内建命令子场景。
 *          Test items:
 *          1. Aggregate `cd` builtin sub-scenarios.
 *          2. Aggregate `ls` builtin sub-scenarios.
 */
#include "terminal_session_test_common.hpp"

void RunTerminalCommandCdTests();
void RunTerminalCommandLsTests();

/**
 * @brief 测试入口函数 `test_terminal_command`。 Test entry function `test_terminal_command`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_terminal_command()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunTerminalCommandCdTests();
  RunTerminalCommandLsTests();
}
