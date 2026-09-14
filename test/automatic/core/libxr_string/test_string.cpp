/**
 * @file test_string.cpp
 * @brief 检查 RuntimeStringView 的文本构造、错误和重新格式化。 /
 * Tests RuntimeStringView text construction, errors and reformatting.
 *
 * 检查内嵌零字节、参数类型限制，以及重新格式化时存储地址保持不变。
 * Checks embedded zero bytes, argument type limits and storage reuse.
 */

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "libxr_def.hpp"
#include "libxr_string.hpp"
#include "test.hpp"
#include "test_assert.hpp"

template <typename T>
constexpr bool accepts_uint32_reformat =
    requires(LibXR::RuntimeStringView<"stamp_{}", std::uint32_t>& view, T value) {
      view.Reformat(value);
    };

template <typename T>
constexpr bool accepts_uint_reprintf =
    requires(LibXR::RuntimeStringView<"stamp_%u", unsigned int>& view, T value) {
      view.Reprintf(value);
    };

static_assert(accepts_uint32_reformat<std::uint32_t>);
static_assert(!accepts_uint32_reformat<std::uint64_t>);
static_assert(accepts_uint_reprintf<unsigned int>);
static_assert(!accepts_uint_reprintf<std::uint64_t>);
static_assert(std::is_move_constructible_v<LibXR::RuntimeStringView<>>);
static_assert(!std::is_move_assignable_v<LibXR::RuntimeStringView<>>);

namespace
{

void TestRuntimeStringText()
{
  // 检查字符串、定长数组和 string_view 的内容；内嵌零字节按输入类型分别处理。
  // Check strings, bounded arrays and string_view; embedded zero bytes follow the input
  // type.
  LibXR::RuntimeStringView<> copied("camera");
  TEST_ASSERT(copied.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!copied.Empty());
  TEST_ASSERT(copied.View() == std::string_view("camera"));
  TEST_ASSERT(copied.CStr()[copied.Size()] == '\0');

  std::string_view copied_view = copied;
  const char* copied_cstr = copied;
  TEST_ASSERT(copied_view == std::string_view("camera"));
  TEST_ASSERT(copied_cstr == copied.CStr());

  LibXR::RuntimeStringView<> empty_text("");
  TEST_ASSERT(empty_text.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(empty_text.Empty());
  TEST_ASSERT(empty_text.View().empty());

  char single_bounded_name[3] = {'i', 'm', 'u'};
  LibXR::RuntimeStringView<> single_bounded(single_bounded_name);
  TEST_ASSERT(single_bounded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!single_bounded.Empty());
  TEST_ASSERT(single_bounded.View() == std::string_view("imu"));

  const char const_single_bounded_name[3] = {'g', 'p', 'u'};
  LibXR::RuntimeStringView<> const_single_bounded(const_single_bounded_name);
  TEST_ASSERT(const_single_bounded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!const_single_bounded.Empty());
  TEST_ASSERT(const_single_bounded.View() == std::string_view("gpu"));

  LibXR::RuntimeStringView<> text_embedded("ab\0cd");
  TEST_ASSERT(text_embedded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!text_embedded.Empty());
  TEST_ASSERT(text_embedded.View() == std::string_view("ab"));

  LibXR::RuntimeStringView<> raw_embedded(std::string_view("ab\0cd", 5));
  TEST_ASSERT(raw_embedded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!raw_embedded.Empty());
  TEST_ASSERT(raw_embedded.View() == std::string_view("ab\0cd", 5));
  TEST_ASSERT(raw_embedded.CStr()[raw_embedded.Size()] == '\0');

  LibXR::RuntimeStringView gyro(std::string_view("camera"), "_gyro");
  TEST_ASSERT(gyro.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!gyro.Empty());
  TEST_ASSERT(gyro.View() == std::string_view("camera_gyro"));

  std::string base = "camera";
  LibXR::RuntimeStringView<> accl(base, "_accl");
  TEST_ASSERT(accl.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!accl.Empty());
  TEST_ASSERT(accl.View() == std::string_view("camera_accl"));

  LibXR::RuntimeStringView<> quat(copied, "_quat");
  TEST_ASSERT(quat.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!quat.Empty());
  TEST_ASSERT(quat.View() == std::string_view("camera_quat"));

  char bounded_name[3] = {'i', 'm', 'u'};
  LibXR::RuntimeStringView<> bounded(bounded_name, "_rx");
  TEST_ASSERT(bounded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!bounded.Empty());
  TEST_ASSERT(bounded.View() == std::string_view("imu_rx"));

  const char const_bounded_name[3] = {'g', 'p', 'u'};
  LibXR::RuntimeStringView<> const_bounded(const_bounded_name, "_tx");
  TEST_ASSERT(const_bounded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!const_bounded.Empty());
  TEST_ASSERT(const_bounded.View() == std::string_view("gpu_tx"));

  char padded_name[8] = {'a', '\0', 'x', 'x'};
  LibXR::RuntimeStringView<> padded(padded_name, "_1");
  TEST_ASSERT(padded.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!padded.Empty());
  TEST_ASSERT(padded.View() == std::string_view("a_1"));

  LibXR::RuntimeStringView<> embedded_text_suffix("ab\0cd", "_x");
  TEST_ASSERT(embedded_text_suffix.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!embedded_text_suffix.Empty());
  TEST_ASSERT(embedded_text_suffix.View() == std::string_view("ab_x"));
}

void TestRuntimeStringErrors()
{
  // 空指针输入返回对应错误，字符串视图和保存结果仍可安全检查。
  // Null inputs return the expected errors and leave views and stored results safe to
  // inspect.
  LibXR::RuntimeStringView<> null_part("camera", static_cast<const char*>(nullptr));
  TEST_ASSERT(null_part.Empty());
  TEST_ASSERT(null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  TEST_ASSERT(null_part.Size() == 0);

  LibXR::RuntimeStringView<> null_copy(static_cast<const char*>(nullptr));
  TEST_ASSERT(null_copy.Empty());
  TEST_ASSERT(null_copy.Status() == LibXR::ErrorCode::PTR_NULL);
  TEST_ASSERT(null_copy.Size() == 0);
  TEST_ASSERT(null_copy.View().empty());
  TEST_ASSERT(null_copy.CStr()[0] == '\0');

  LibXR::RuntimeStringView<> bare_null(nullptr);
  TEST_ASSERT(bare_null.Empty());
  TEST_ASSERT(bare_null.Status() == LibXR::ErrorCode::PTR_NULL);
  TEST_ASSERT(bare_null.View().empty());

  LibXR::RuntimeStringView<> bare_null_part("camera", nullptr);
  TEST_ASSERT(bare_null_part.Empty());
  TEST_ASSERT(bare_null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  TEST_ASSERT(bare_null_part.View().empty());
}

void TestRuntimeStringFormat()
{
  // 反复格式化到同一对象，检查新文本正确，且 CStr 返回的存储地址保持不变。
  // Reformat the same object; check the new text and that CStr keeps the same storage
  // address.
  LibXR::RuntimeStringView<"camera_{}", unsigned int> formatted;
  TEST_ASSERT(formatted.Reformat(7U) == LibXR::ErrorCode::OK);
  TEST_ASSERT(formatted.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!formatted.Empty());
  TEST_ASSERT(formatted.View() == std::string_view("camera_7"));
  TEST_ASSERT(formatted.CStr()[formatted.Size()] == '\0');

  LibXR::RuntimeStringView<"frame_%03u", unsigned int> printf_formatted;
  TEST_ASSERT(printf_formatted.Reprintf(5U) == LibXR::ErrorCode::OK);
  TEST_ASSERT(printf_formatted.Status() == LibXR::ErrorCode::OK);
  TEST_ASSERT(!printf_formatted.Empty());
  TEST_ASSERT(printf_formatted.View() == std::string_view("frame_005"));
  TEST_ASSERT(printf_formatted.CStr()[printf_formatted.Size()] == '\0');

  LibXR::RuntimeStringView<"stamp_%u", unsigned int> timestamp;
  TEST_ASSERT(timestamp.Reprintf(1U) == LibXR::ErrorCode::OK);
  const char* timestamp_storage = timestamp.CStr();
  TEST_ASSERT(timestamp.Reprintf(1234567890U) == LibXR::ErrorCode::OK);
  TEST_ASSERT(timestamp.CStr() == timestamp_storage);
  TEST_ASSERT(timestamp.View() == std::string_view("stamp_1234567890"));

  LibXR::RuntimeStringView<"stamp_{}", std::uint32_t> format_timestamp;
  TEST_ASSERT(format_timestamp.Reformat(std::uint32_t{1}) == LibXR::ErrorCode::OK);
  const char* format_storage = format_timestamp.CStr();
  TEST_ASSERT(format_timestamp.Reformat(std::numeric_limits<std::uint32_t>::max()) ==
              LibXR::ErrorCode::OK);
  TEST_ASSERT(format_timestamp.CStr() == format_storage);
  TEST_ASSERT(format_timestamp.View() == std::string_view("stamp_4294967295"));

  LibXR::RuntimeStringView<"float_%.0f", float> float_fixed;
  TEST_ASSERT(float_fixed.Reprintf(1.0F) == LibXR::ErrorCode::OK);
  const char* float_storage = float_fixed.CStr();
  TEST_ASSERT(float_fixed.Reprintf(12345.0F) == LibXR::ErrorCode::OK);
  TEST_ASSERT(float_fixed.CStr() == float_storage);
  TEST_ASSERT(float_fixed.View() == std::string_view("float_12345"));
}

}  // namespace

void test_string()
{
  TestRuntimeStringText();
  TestRuntimeStringErrors();
  TestRuntimeStringFormat();
}
