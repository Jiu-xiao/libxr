/**
 * @file test_string_format.cpp
 * @brief 运行时字符串重格式化场景子测试。 Split test unit for runtime-string reformat/reprintf scenarios.
 * @details 测试项目：
 *          1. `Reformat` / `Reprintf` 生成预期文本。
 *          2. 多次重格式化复用同一块稳定存储。
 *          3. 浮点与整数边界值保持可用输出。
 *          Test items:
 *          1. `Reformat` / `Reprintf` produce the expected text.
 *          2. Repeated formatting reuses the same stable storage buffer.
 *          3. Floating-point and integer edge values keep producing valid output.
 */
#include "string_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestRuntimeStringFormat`。 Test-item function `TestRuntimeStringFormat`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestRuntimeStringFormat()
{
  // 测试内容：验证运行时格式化输出、稳定存储和边界值路径。
  // Test coverage: verify runtime formatting output, stable storage reuse, and boundary-value paths.
  LibXR::RuntimeStringView<"camera_{}", unsigned int> formatted;
  ASSERT(formatted.Reformat(7U) == LibXR::ErrorCode::OK);
  ASSERT(formatted.Status() == LibXR::ErrorCode::OK);
  ASSERT(!formatted.Empty());
  ASSERT(formatted.View() == std::string_view("camera_7"));
  ASSERT(formatted.CStr()[formatted.Size()] == '\0');

  LibXR::RuntimeStringView<"frame_%03u", unsigned int> printf_formatted;
  ASSERT(printf_formatted.Reprintf(5U) == LibXR::ErrorCode::OK);
  ASSERT(printf_formatted.Status() == LibXR::ErrorCode::OK);
  ASSERT(!printf_formatted.Empty());
  ASSERT(printf_formatted.View() == std::string_view("frame_005"));
  ASSERT(printf_formatted.CStr()[printf_formatted.Size()] == '\0');

  LibXR::RuntimeStringView<"stamp_%u", unsigned int> timestamp;
  ASSERT(timestamp.Reprintf(1U) == LibXR::ErrorCode::OK);
  const char* timestamp_storage = timestamp.CStr();
  ASSERT(timestamp.Reprintf(1234567890U) == LibXR::ErrorCode::OK);
  ASSERT(timestamp.CStr() == timestamp_storage);
  ASSERT(timestamp.View() == std::string_view("stamp_1234567890"));

  LibXR::RuntimeStringView<"stamp_{}", std::uint32_t> format_timestamp;
  ASSERT(format_timestamp.Reformat(std::uint32_t{1}) == LibXR::ErrorCode::OK);
  const char* format_storage = format_timestamp.CStr();
  ASSERT(format_timestamp.Reformat(std::numeric_limits<std::uint32_t>::max()) ==
         LibXR::ErrorCode::OK);
  ASSERT(format_timestamp.CStr() == format_storage);
  ASSERT(format_timestamp.View() == std::string_view("stamp_4294967295"));

  LibXR::RuntimeStringView<"float_%.0f", float> float_fixed;
  ASSERT(float_fixed.Reprintf(1.0F) == LibXR::ErrorCode::OK);
  const char* float_storage = float_fixed.CStr();
  ASSERT(float_fixed.Reprintf(std::numeric_limits<float>::max()) ==
         LibXR::ErrorCode::OK);
  ASSERT(float_fixed.CStr() == float_storage);
  ASSERT(float_fixed.Size() > 35);
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeStringFormatTests`。 Test-item function `RunRuntimeStringFormatTests`.
 * @details 测试内容：执行运行时字符串重格式化子场景。 Execute runtime-string reformat/reprintf sub-scenarios.
 *          测试原理：把格式化路径单独成组，聚焦文本生成与稳定存储契约。 Group formatting paths around text-generation and stable-storage contracts.
 */
void RunRuntimeStringFormatTests()
{
  TestRuntimeStringFormat();
}
