/**
 * @file test_linux_stdio_print.cpp
 * @brief Linux STDIO `print` 适配层测试入口。 Entry point for Linux STDIO `print`
 * adapter tests.
 * @details 这组测试不重复覆盖格式语法；它验证 `LibXR::STDIO` 的全局写端绑定、直接写入、
 *          stream-backed 写入和容量截断契约。 This group does not repeat format-syntax
 *          coverage; it verifies `LibXR::STDIO` global write binding, direct writes,
 *          stream-backed writes, and capacity truncation contracts.
 */
#include "linux_stdio_print_test_common.hpp"

/**
 * @brief 执行 Linux STDIO `print` wrapper 和截断测试。 Run Linux STDIO `print` wrapper
 * and truncation tests.
 */
void test_linux_stdio_print()
{
  // 先测正常写入路径，再测容量不足时返回值和保留内容。
  // Check normal write paths first, then truncation return values and retained payload.
  LibXRLinuxStdioPrintTest::TestStdioPrintWrappers();
  LibXRLinuxStdioPrintTest::TestStdioTruncation();
}
