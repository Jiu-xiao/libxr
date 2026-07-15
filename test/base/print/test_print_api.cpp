/**
 * @file test_print_api.cpp
 * @brief `Print::*` 公开 API wrapper 的运行时契约测试。 Runtime contract tests for
 * public `Print::*` API wrappers.
 * @details
 * 1. 不重复扩大格式语法覆盖。
 * 2. 验证已编译格式写入 sink、bounded buffer 和 `SNPrintf` 风格接口。
 * 3. 验证截断长度和错误返回。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 覆盖 public print wrapper 的 sink、buffer、截断和错误传播路径。 Cover sink,
 * buffer, truncation, and error-propagation paths in the public print wrappers.
 */
void TestPrintApiWrappers()
{
  // Sink wrapper：已构造 format 对象和字面量模板两种入口都应写入同一 sink 合约。
  // Sink wrappers: object-based and literal-template entry points share the same sink
  // contract.
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

  // Bounded buffer：返回完整逻辑长度，实际内容按容量写入并保持 C 字符串结尾语义。
  // Bounded buffers: return full logical length while writing only what fits as a C
  // string.
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

  // 边界容量：零容量、单字节容量和空指针计数模式不应越界写入。
  // Capacity edges: zero-capacity, single-byte, and null-counting modes must not
  // over-write.
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

  // `SNPrintf` wrapper：保留 snprintf 风格的返回值和截断后的可见内容。
  // `SNPrintf` wrappers preserve snprintf-style return values and truncated visible
  // content.
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

  // 错误传播：sink 写入失败或格式运行期失败时，wrapper 返回错误并清理 buffer 开头。
  // Error propagation: sink and runtime-format failures are surfaced and buffers are
  // cleared.
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
    int written =
        LibXR::Print::FormatIntoBuffer(buffer, sizeof(buffer), BrokenGenericFormat{});
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

  // 空输出：成功写入空字符串时返回 0，buffer 仍保持 NUL 开头。
  // Empty output returns zero and leaves the buffer NUL-terminated at the first byte.
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
