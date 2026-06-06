/**
 * @file test_printf_frontend.cpp
 * @brief `print` printf 前端语义聚合入口。 Aggregation entry for split `print` printf frontend semantics.
 * @details 测试项目：
 *          1. 聚合整数族语义子场景。
 *          2. 聚合文本/杂项语义子场景。
 *          3. 聚合浮点族语义子场景。
 *          Test items:
 *          1. Aggregate integer-family semantic sub-scenarios.
 *          2. Aggregate text/misc semantic sub-scenarios.
 *          3. Aggregate floating-point semantic sub-scenarios.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
void TestPrintfFrontendIntegerSemantics();
void TestPrintfFrontendTextSemantics();
void TestPrintfFrontendFloatSemantics();

/**
 * @brief 测试项函数 `TestPrintfFrontendSemantics`。 Test-item function `TestPrintfFrontendSemantics`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPrintfFrontendSemantics()
{
  // 测试内容：聚合执行拆分后的整数、文本和浮点前端语义子场景。
  // Test coverage: aggregate the split integer, text, and floating-point frontend semantic sub-scenarios.
  TestPrintfFrontendIntegerSemantics();
  TestPrintfFrontendTextSemantics();
  TestPrintfFrontendFloatSemantics();
}
}  // namespace LibXRPrintTest
