/**
 * @file test_print.cpp
 * @brief 打印测试入口，依次调用两个格式前端、公共 API 和写入失败测试。 /
 * Print test entry for both format frontends, public APIs and writer failures.
 */

#include "core/print/print_test_common.hpp"

void test_print()
{
  // 按分类顺序执行，便于失败时先定位到子系统再看具体断言。
  // Keep the category order explicit so failures point at the affected subsystem first.
  LibXRPrintTest::TestPrintfFrontendSemantics();
  LibXRPrintTest::TestFormatFrontendSemantics();
  LibXRPrintTest::TestPrintApiWrappers();
  LibXRPrintTest::TestStreamBackedPrintFailureKeepsPrefix();
}
