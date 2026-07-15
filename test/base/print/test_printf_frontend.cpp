/**
 * @file test_printf_frontend.cpp
 * @brief 默认 profile 的 `printf` 前端运行时输出聚合。 Default-profile `printf`
 * frontend runtime-output aggregation.
 * @details
 * 1. 覆盖 `printf` 字面量 -> 编译格式 -> writer 输出。
 * 2. 只测默认配置运行时输出。
 * 3. 禁用宏矩阵在 `test/print_config`。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
void TestPrintfFrontendIntegerSemantics();
void TestPrintfFrontendTextSemantics();
void TestPrintfFrontendFloatSemantics();

/**
 * @brief 执行 `printf` 默认输出语义的三组子场景。 Run the three default-output
 * `printf` semantic groups.
 * @details 整数、文本/杂项、浮点分别成文件，避免一个失败掩盖具体格式族。
 */
void TestPrintfFrontendSemantics()
{
  // 默认 profile 下的三类 printf 输出语义。
  // Three printf output families under the default profile.
  TestPrintfFrontendIntegerSemantics();
  TestPrintfFrontendTextSemantics();
  TestPrintfFrontendFloatSemantics();
}
}  // namespace LibXRPrintTest
