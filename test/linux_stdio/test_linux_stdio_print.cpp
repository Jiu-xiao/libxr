/**
 * @file test_linux_stdio_print.cpp
 * @brief linux stdio `print` 测试聚合入口。 Aggregation entry for split linux stdio `print` tests.
 */
#include "linux_stdio_print_test_common.hpp"

/**
 * @brief 测试入口函数 `test_linux_stdio_print`。 Test entry function `test_linux_stdio_print`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_linux_stdio_print()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXRLinuxStdioPrintTest::TestStdioPrintWrappers();
  LibXRLinuxStdioPrintTest::TestStdioTruncation();
}
