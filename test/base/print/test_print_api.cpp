/**
 * @file test_print_api.cpp
 * @brief `print` 公开 API 包装层子测试。 Split test unit for `print` public API wrappers.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 测试项函数 `TestPrintApiWrappers`。 Test-item function `TestPrintApiWrappers`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPrintApiWrappers()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  {
    constexpr LibXR::Format<"x={:+05d} {:#x} {}"> format{};
    StringSink sink;
    auto ec = LibXR::Print::FormatTo(sink, format, 7, 42U, "ok");
    if (ec != LibXR::ErrorCode::OK || sink.buffer != "x=+0007 0x2a ok")
    {
      Fail("format sink wrapper mismatch");
    }
  }

  {
    constexpr auto format = LibXR::Print::Printf::Build<"%+05d %#x %s">();
    StringSink sink;
    auto ec = LibXR::Print::FormatTo(sink, format, 7, 42U, "ok");
    if (ec != LibXR::ErrorCode::OK || sink.buffer != "+0007 0x2a ok")
    {
      Fail("printf sink wrapper mismatch");
    }
  }

  {
    StringSink sink;
    auto ec = LibXR::Print::FormatTo<"x={:+05d} {:#x} {}">(sink, 7, 42U, "ok");
    if (ec != LibXR::ErrorCode::OK || sink.buffer != "x=+0007 0x2a ok")
    {
      Fail("format literal sink wrapper mismatch");
    }
  }

  {
    StringSink sink;
    auto ec = LibXR::Print::PrintfTo<"%+05d %#x %s">(sink, 7, 42U, "ok");
    if (ec != LibXR::ErrorCode::OK || sink.buffer != "+0007 0x2a ok")
    {
      Fail("printf literal sink wrapper mismatch");
    }
  }

  {
    constexpr LibXR::Format<"x={:+05d} {:#x} {}"> format{};
    char buffer[32] = {};
    int written =
        LibXR::Print::FormatIntoBuffer(buffer, sizeof(buffer), format, 7, 42U, "ok");
    if (std::string_view(buffer) != "x=+0007 0x2a ok" ||
        written != static_cast<int>(std::strlen("x=+0007 0x2a ok")))
    {
      Fail("format to bounded buffer mismatch");
    }
  }

  {
    constexpr auto format = LibXR::Print::Printf::Build<"%d %s">();
    char buffer[6] = {};
    int written =
        LibXR::Print::FormatIntoBuffer(buffer, sizeof(buffer), format, 123, "xy");
    if (std::string_view(buffer) != "123 x" ||
        written != static_cast<int>(std::strlen("123 xy")))
    {
      Fail("printf truncating bounded buffer mismatch");
    }
  }

  {
    char buffer[32] = {};
    int written = LibXR::Print::FormatIntoBuffer<"x={:+05d} {:#x} {}">(
        buffer, sizeof(buffer), 7, 42U, "ok");
    if (std::string_view(buffer) != "x=+0007 0x2a ok" ||
        written != static_cast<int>(std::strlen("x=+0007 0x2a ok")))
    {
      Fail("format literal bounded buffer mismatch");
    }
  }

  {
    char buffer[16] = {};
    int written =
        LibXR::Print::PrintfIntoBuffer<"%d %s">(buffer, sizeof(buffer), 123, "xy");
    if (std::string_view(buffer) != "123 xy" ||
        written != static_cast<int>(std::strlen("123 xy")))
    {
      Fail("printf literal bounded buffer mismatch");
    }
  }

  {
    constexpr LibXR::Format<"{} {}"> format{};
    char buffer[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', '\0'};
    int written = LibXR::Print::FormatIntoBuffer(buffer, 0, format, 123, "xy");
    if (written != static_cast<int>(std::strlen("123 xy")) || buffer[0] != 'x')
    {
      Fail("format zero-capacity buffer mismatch");
    }
  }

  {
    char buffer[4] = {'x', 'x', 'x', '\0'};
    int written = LibXR::Print::PrintfIntoBuffer<"%d %s">(buffer, 1, 123, "xy");
    if (written != static_cast<int>(std::strlen("123 xy")) || buffer[0] != '\0')
    {
      Fail("printf single-capacity buffer mismatch");
    }
  }

  {
    constexpr LibXR::Format<"{} {}"> format{};
    int written = LibXR::Print::FormatIntoBuffer(nullptr, 0, format, 123, "xy");
    if (written != static_cast<int>(std::strlen("123 xy")))
    {
      Fail("format null counting buffer mismatch");
    }
  }

  {
    constexpr LibXR::Format<"{} {}"> format{};
    char buffer[6] = {};
    int written = LibXR::Print::SNPrintf(buffer, sizeof(buffer), format, 123, "xy");
    if (written != static_cast<int>(std::strlen("123 xy")) ||
        std::string_view(buffer) != "123 x")
    {
      Fail("snprintf style wrapper mismatch");
    }
  }

  {
    char buffer[6] = {};
    int written = LibXR::Print::SNPrintf<"%d %s">(buffer, sizeof(buffer), 123, "xy");
    if (written != static_cast<int>(std::strlen("123 xy")) ||
        std::string_view(buffer) != "123 x")
    {
      Fail("printf literal snprintf wrapper mismatch");
    }
  }

  {
    constexpr LibXR::Format<"hello {}"> format{};
    LimitedSink sink{.limit = 6};
    auto ec = LibXR::Print::FormatTo(sink, format, "xy");
    if (ec != LibXR::ErrorCode::NO_BUFF || sink.buffer != "hello ")
    {
      Fail("format sink error propagation mismatch");
    }
  }

  {
    LimitedSink sink{.limit = 5};
    auto ec = LibXR::Print::PrintfTo<"%d %s">(sink, 123, "xy");
    if (ec != LibXR::ErrorCode::NO_BUFF || sink.buffer != "123 ")
    {
      Fail("printf sink error propagation mismatch");
    }
  }

  {
    char buffer[8] = {'x', 'x', 'x', '\0'};
    int written = LibXR::Print::FormatIntoBuffer(buffer, sizeof(buffer), BrokenGenericFormat{});
    if (written != -1 || buffer[0] != '\0')
    {
      Fail("format bounded buffer runtime error mismatch");
    }
  }

  {
    char buffer[8] = {'x', 'x', 'x', '\0'};
    int written = LibXR::Print::SNPrintf(buffer, sizeof(buffer), BrokenGenericFormat{});
    if (written != -1 || buffer[0] != '\0')
    {
      Fail("snprintf runtime error mismatch");
    }
  }

  {
    constexpr LibXR::Format<""> format{};
    char buffer[4] = {'x', 'x', 'x', '\0'};
    int written = LibXR::Print::FormatIntoBuffer(buffer, sizeof(buffer), format);
    if (written != 0 || buffer[0] != '\0')
    {
      Fail("format bounded buffer empty output mismatch");
    }
  }
}
}  // namespace LibXRPrintTest
