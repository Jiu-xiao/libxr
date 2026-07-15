/**
 * @file test_print.cpp
 * @brief 默认 profile 的 `print` 运行时输出测试入口。 Entry point for default-profile
 * `print` runtime output tests.
 *
 * 分类 / Categories:
 * 1. `printf` 前端：字面量解析后写入 writer，并与 host `snprintf` 或固定期望文本对照。
 *    `printf` frontend: parse literals, write through the writer, and compare with host
 *    `snprintf` or fixed expected text.
 * 2. brace `Format` 前端：验证 `{}` 风格语法的编译结果和输出文本。
 *    Brace `Format` frontend: verify compiled `{}`-style formats and produced text.
 * 3. `Print::*` 公开 API：覆盖 sink、字面量、bounded-buffer、`SNPrintf` 和错误返回契约。
 *    Public `Print::*` API: cover sink, literal, bounded-buffer, `SNPrintf`, and error
 *    contracts.
 * 4. 失败路径：stream-backed writer 遇到后续错误时已经写出的前缀不回滚。
 *    Failure path: a stream-backed writer keeps the prefix emitted before a later error.
 */
#include "print_test_common.hpp"

/**
 * @brief 执行默认 profile 的四组 `print` 运行时测试。 Run the four default-profile
 * `print` runtime groups.
 * @details 入口只负责调度；断言分别放在各子文件中。
 *          This entry only dispatches.
 */
void test_print()
{
  // 按分类顺序执行，便于失败时先定位到子系统再看具体断言。
  // Keep the category order explicit so failures point at the affected subsystem first.
  LibXRPrintTest::TestPrintfFrontendSemantics();
  LibXRPrintTest::TestFormatFrontendSemantics();
  LibXRPrintTest::TestPrintApiWrappers();
  LibXRPrintTest::TestStreamBackedPrintFailureKeepsPrefix();
}
