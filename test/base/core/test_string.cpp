/**
 * @file test_string.cpp
 * @brief 运行时字符串视图构造与重格式化测试。 Runtime string-view construction and reformatting tests.
 *
 * 测试项目 / Test items:
 * 1. 普通文本、定长数组、嵌入 NUL 与后缀拼接。 Text construction: verify plain text, bounded arrays, embedded-NUL spans and suffix concatenation keep the expected retained view.
 * 2. 空指针错误路径。 Error handling: verify null-pointer inputs report `PTR_NULL` and collapse to empty runtime strings safely.
 * 3. `format` / `printf` 重格式化。 Reformat/reprintf: verify compiled `format` and `printf` refresh the same storage and produce the expected textual output.
 *
 * 测试原理 / Test principles:
 * 1. 联合观察状态、`View()` 和 `CStr()`，因为该类型把内容和稳定存储绑在一起。 Observe status, `View()` and `CStr()` together, because this type couples content and lifetime-stable storage.
 * 2. 故意使用定长数组和嵌入 NUL 输入，覆盖最容易和文本抽象冲突的场景。 Use bounded arrays and embedded-NUL inputs deliberately, since those are the cases where string/text abstractions usually drift from byte-span semantics.
 */
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "libxr_def.hpp"
#include "libxr_string.hpp"
#include "test.hpp"

template <typename T>
constexpr bool accepts_uint32_reformat =
    requires(LibXR::RuntimeStringView<"stamp_{}", std::uint32_t>& view, T value)
{
  view.Reformat(value);
};

template <typename T>
constexpr bool accepts_uint_reprintf =
    requires(LibXR::RuntimeStringView<"stamp_%u", unsigned int>& view, T value)
{
  view.Reprintf(value);
};

static_assert(accepts_uint32_reformat<std::uint32_t>);
static_assert(!accepts_uint32_reformat<std::uint64_t>);
static_assert(accepts_uint_reprintf<unsigned int>);
static_assert(!accepts_uint_reprintf<std::uint64_t>);
static_assert(std::is_move_constructible_v<LibXR::RuntimeStringView<>>);
static_assert(!std::is_move_assignable_v<LibXR::RuntimeStringView<>>);

/**
 * @brief 测试项函数 `TestRuntimeStringText`。 Test-item function `TestRuntimeStringText`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
static void TestRuntimeStringText()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestRuntimeStringErrors`。 Test-item function `TestRuntimeStringErrors`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
static void TestRuntimeStringErrors()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  LibXR::RuntimeStringView<> null_part("camera", static_cast<const char*>(nullptr));
  ASSERT(null_part.Empty());
  ASSERT(null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_part.Size() == 0);

  LibXR::RuntimeStringView<> null_copy(static_cast<const char*>(nullptr));
  ASSERT(null_copy.Empty());
  ASSERT(null_copy.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_copy.Size() == 0);
  ASSERT(null_copy.View().empty());
  ASSERT(null_copy.CStr()[0] == '\0');

  LibXR::RuntimeStringView<> bare_null(nullptr);
  ASSERT(bare_null.Empty());
  ASSERT(bare_null.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(bare_null.View().empty());

  LibXR::RuntimeStringView<> bare_null_part("camera", nullptr);
  ASSERT(bare_null_part.Empty());
  ASSERT(bare_null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(bare_null_part.View().empty());
}

/**
 * @brief 测试项函数 `TestRuntimeStringFormat`。 Test-item function `TestRuntimeStringFormat`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
static void TestRuntimeStringFormat()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试入口函数 `test_string`。 Test entry function `test_string`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_string()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestRuntimeStringText();
  TestRuntimeStringErrors();
  TestRuntimeStringFormat();
}
