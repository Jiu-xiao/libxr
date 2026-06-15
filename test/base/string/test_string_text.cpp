/**
 * @file test_string_text.cpp
 * @brief 运行时字符串文本构造场景子测试。 Split test unit for runtime-string text construction scenarios.
 * @details 测试项目：
 *          1. 普通文本、定长数组和空字符串构造。
 *          2. 嵌入 NUL 输入与带后缀拼接构造。
 *          Test items:
 *          1. Plain text, bounded-array, and empty-string construction.
 *          2. Embedded-NUL input and suffix-appending construction.
 */
#include "string_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestRuntimeStringText`。 Test-item function `TestRuntimeStringText`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestRuntimeStringText()
{
  // 测试内容：验证不同文本来源都能保持预期视图和稳定存储。
  // Test coverage: verify that different text sources preserve the expected view and stable storage.
  LibXR::RuntimeStringView<> copied("camera");
  ASSERT(copied.Status() == LibXR::ErrorCode::OK);
  ASSERT(!copied.Empty());
  ASSERT(copied.View() == std::string_view("camera"));
  ASSERT(copied.CStr()[copied.Size()] == '\0');

  std::string_view copied_view = copied;
  const char* copied_cstr = copied;
  ASSERT(copied_view == std::string_view("camera"));
  ASSERT(copied_cstr == copied.CStr());

  LibXR::RuntimeStringView<> empty_text("");
  ASSERT(empty_text.Status() == LibXR::ErrorCode::OK);
  ASSERT(empty_text.Empty());
  ASSERT(empty_text.View().empty());

  char single_bounded_name[3] = {'i', 'm', 'u'};
  LibXR::RuntimeStringView<> single_bounded(single_bounded_name);
  ASSERT(single_bounded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!single_bounded.Empty());
  ASSERT(single_bounded.View() == std::string_view("imu"));

  const char const_single_bounded_name[3] = {'g', 'p', 'u'};
  LibXR::RuntimeStringView<> const_single_bounded(const_single_bounded_name);
  ASSERT(const_single_bounded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!const_single_bounded.Empty());
  ASSERT(const_single_bounded.View() == std::string_view("gpu"));

  LibXR::RuntimeStringView<> text_embedded("ab\0cd");
  ASSERT(text_embedded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!text_embedded.Empty());
  ASSERT(text_embedded.View() == std::string_view("ab"));

  LibXR::RuntimeStringView<> raw_embedded(std::string_view("ab\0cd", 5));
  ASSERT(raw_embedded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!raw_embedded.Empty());
  ASSERT(raw_embedded.View() == std::string_view("ab\0cd", 5));
  ASSERT(raw_embedded.CStr()[raw_embedded.Size()] == '\0');

  LibXR::RuntimeStringView gyro(std::string_view("camera"), "_gyro");
  ASSERT(gyro.Status() == LibXR::ErrorCode::OK);
  ASSERT(!gyro.Empty());
  ASSERT(gyro.View() == std::string_view("camera_gyro"));

  std::string base = "camera";
  LibXR::RuntimeStringView<> accl(base, "_accl");
  ASSERT(accl.Status() == LibXR::ErrorCode::OK);
  ASSERT(!accl.Empty());
  ASSERT(accl.View() == std::string_view("camera_accl"));

  LibXR::RuntimeStringView<> quat(copied, "_quat");
  ASSERT(quat.Status() == LibXR::ErrorCode::OK);
  ASSERT(!quat.Empty());
  ASSERT(quat.View() == std::string_view("camera_quat"));

  char bounded_name[3] = {'i', 'm', 'u'};
  LibXR::RuntimeStringView<> bounded(bounded_name, "_rx");
  ASSERT(bounded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!bounded.Empty());
  ASSERT(bounded.View() == std::string_view("imu_rx"));

  const char const_bounded_name[3] = {'g', 'p', 'u'};
  LibXR::RuntimeStringView<> const_bounded(const_bounded_name, "_tx");
  ASSERT(const_bounded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!const_bounded.Empty());
  ASSERT(const_bounded.View() == std::string_view("gpu_tx"));

  char padded_name[8] = {'a', '\0', 'x', 'x'};
  LibXR::RuntimeStringView<> padded(padded_name, "_1");
  ASSERT(padded.Status() == LibXR::ErrorCode::OK);
  ASSERT(!padded.Empty());
  ASSERT(padded.View() == std::string_view("a_1"));

  LibXR::RuntimeStringView<> embedded_text_suffix("ab\0cd", "_x");
  ASSERT(embedded_text_suffix.Status() == LibXR::ErrorCode::OK);
  ASSERT(!embedded_text_suffix.Empty());
  ASSERT(embedded_text_suffix.View() == std::string_view("ab_x"));
}

}  // namespace

/**
 * @brief 测试项函数 `RunRuntimeStringTextTests`。 Test-item function `RunRuntimeStringTextTests`.
 * @details 测试内容：执行运行时字符串文本构造子场景。 Execute runtime-string text-construction sub-scenarios.
 *          测试原理：把文本构造语义单独成组，避免与错误路径和重格式化路径缠在一起。 Group text-construction semantics away from error and reformatting paths.
 */
void RunRuntimeStringTextTests()
{
  TestRuntimeStringText();
}
