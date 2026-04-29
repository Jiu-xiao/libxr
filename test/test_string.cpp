#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

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

static void TestRuntimeStringText()
{
  LibXR::RuntimeStringView<> copied("camera");
  ASSERT(copied.Ok());
  ASSERT(copied.View() == std::string_view("camera"));
  ASSERT(copied.CStr()[copied.Size()] == '\0');

  std::string_view copied_view = copied;
  const char* copied_cstr = copied;
  ASSERT(copied_view == std::string_view("camera"));
  ASSERT(copied_cstr == copied.CStr());

  LibXR::RuntimeStringView gyro(std::string_view("camera"), "_gyro");
  ASSERT(gyro.Ok());
  ASSERT(gyro.View() == std::string_view("camera_gyro"));

  std::string base = "camera";
  LibXR::RuntimeStringView<> accl(base, "_accl");
  ASSERT(accl.Ok());
  ASSERT(accl.View() == std::string_view("camera_accl"));

  LibXR::RuntimeStringView<> quat(copied, "_quat");
  ASSERT(quat.Ok());
  ASSERT(quat.View() == std::string_view("camera_quat"));
}

static void TestRuntimeStringErrors()
{
  LibXR::RuntimeStringView<> null_part("camera", static_cast<const char*>(nullptr));
  ASSERT(!null_part.Ok());
  ASSERT(null_part.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_part.Size() == 0);

  LibXR::RuntimeStringView<> null_copy(static_cast<const char*>(nullptr));
  ASSERT(!null_copy.Ok());
  ASSERT(null_copy.Status() == LibXR::ErrorCode::PTR_NULL);
  ASSERT(null_copy.Size() == 0);
  ASSERT(null_copy.View().empty());
  ASSERT(null_copy.CStr()[0] == '\0');
}

static void TestRuntimeStringFormat()
{
  LibXR::RuntimeStringView<"camera_{}", unsigned int> formatted;
  ASSERT(formatted.Reformat(7U) == LibXR::ErrorCode::OK);
  ASSERT(formatted.Ok());
  ASSERT(formatted.View() == std::string_view("camera_7"));
  ASSERT(formatted.CStr()[formatted.Size()] == '\0');

  LibXR::RuntimeStringView<"frame_%03u", unsigned int> printf_formatted;
  ASSERT(printf_formatted.Reprintf(5U) == LibXR::ErrorCode::OK);
  ASSERT(printf_formatted.Ok());
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

void test_string()
{
  TestRuntimeStringText();
  TestRuntimeStringErrors();
  TestRuntimeStringFormat();
}
