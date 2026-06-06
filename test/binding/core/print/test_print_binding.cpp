/**
 * @file test_print_binding.cpp
 * @brief binding `print` 测试聚合入口。 Aggregation entry for split binding `print` tests.
 */
#include "print_binding_test_common.hpp"

/**
 * @brief 测试入口函数 `test_print_binding`。 Test entry function `test_print_binding`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_print_binding()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXRBindingPrintTest::TestStdioPrintWrappers();
  LibXRBindingPrintTest::TestStdioTruncation();
}
