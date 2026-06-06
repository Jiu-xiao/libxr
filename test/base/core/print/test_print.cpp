/**
 * @file test_print.cpp
 * @brief base 平面 `print` 前端与 writer 执行测试。 `print` frontends and writer execution tests on the base plane.
 *
 * 测试项目 / Test items:
 * 1. `printf` 前端 lowering 语义。 `printf` frontend lowering: verify integer, float, width, precision and flag semantics after compile-time parsing.
 * 2. `format` 前端 lowering 语义。 `format` frontend lowering: verify brace-style field parsing, opcode generation and rendered output semantics.
 * 3. 公开 print API 包装层行为。 Print API wrappers: verify the public formatting helpers route the compiled format into the expected sink behavior.
 * 4. 失败路径下的前缀保留语义。 Failure-path prefix retention: verify stream-backed output keeps the already-written prefix when later formatting fails.
 *
 * 测试原理 / Test principles:
 * 1. 以最终渲染文本为主验证对象，因为这个子系统最终契约就是输出字节流。 Validate rendered text instead of parser internals alone, because the real contract of this subsystem is the final byte stream.
 * 2. 同时覆盖成功和失败写会话，因为失败时的可观察语义也是 API 的一部分。 Cover both successful and failing write sessions, since the observable error-handling semantics are part of the API surface.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "libxr.hpp"

static_assert(LibXR::Format<"abc">::ArgumentCount() == 0);
static_assert(LibXR::Format<"{1} {0}">::ArgumentCount() == 2);
static_assert(LibXR::Format<"{:d} {}">::template Matches<int, const char*>());
static_assert(LibXR::Format<"{}">::template Matches<int>());
static_assert(!LibXR::Format<"{}">::template Matches<int, int>());
static_assert(!LibXR::Format<"abc">::template Matches<int>());
static_assert(!LibXR::Format<"{:d} {}">::template Matches<const char*, int>());

using LoggerFrontend = LibXR::Detail::LoggerLiteral::Frontend;
using LoggerResolution = LibXR::Detail::LoggerLiteral::Resolution;

static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger {}", int>() ==
              LoggerResolution::Format);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger %d", int>() ==
              LoggerResolution::Printf);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "value=%u", unsigned>() ==
              LoggerResolution::Printf);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{{}}">() ==
    LoggerResolution::Format);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "%%">() ==
    LoggerResolution::Printf);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "plain text">() ==
    LoggerResolution::Format);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "plain text", int>() ==
              LoggerResolution::None);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "logger {}", int, int>() ==
              LoggerResolution::None);
#if LIBXR_PRINT_INTEGER_ENABLE_64BIT
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<
                  LoggerFrontend::Auto, "frame=%llu", unsigned long long>() ==
              LoggerResolution::Printf);
#else
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<
                  LoggerFrontend::Auto, "frame=%llu", unsigned long long>() ==
              LoggerResolution::None);
#endif
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{} %d", int>() ==
    LoggerResolution::Ambiguous);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "%s {}",
                                                            const char*>() ==
              LoggerResolution::Ambiguous);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "{0}%1$d", int>() ==
              LoggerResolution::Ambiguous);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{%">() ==
    LoggerResolution::None);
static_assert(
    LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto, "{%d}", int>() ==
    LoggerResolution::Printf);
static_assert(LibXR::Detail::LoggerLiteral::ResolveFrontend<LoggerFrontend::Auto,
                                                            "{123abc} %d", int>() ==
              LoggerResolution::Printf);

namespace
{
struct StringSink
{
  LibXR::ErrorCode Write(std::string_view text)
  {
    buffer.append(text.data(), text.size());
    return LibXR::ErrorCode::OK;
  }

  std::string buffer;
};

struct LimitedSink
{
  size_t limit = 0;
  std::string buffer;

  LibXR::ErrorCode Write(std::string_view text)
  {
    if (buffer.size() + text.size() > limit)
    {
      return LibXR::ErrorCode::NO_BUFF;
    }
    buffer.append(text.data(), text.size());
    return LibXR::ErrorCode::OK;
  }
};

struct BrokenGenericFormat
{
  template <typename... Args>
  [[nodiscard]] static consteval bool Matches()
  {
    return sizeof...(Args) == 0;
  }

  [[nodiscard]] static constexpr auto Codes()
  {
    return std::to_array<uint8_t>({
        static_cast<uint8_t>(LibXR::Print::FormatOp::GenericField),
        static_cast<uint8_t>(LibXR::Print::FormatType::End),
        0,
        static_cast<uint8_t>(' '),
        0,
        0xFF,
        static_cast<uint8_t>(LibXR::Print::FormatOp::End),
    });
  }

  [[nodiscard]] static constexpr auto ArgumentList()
  {
    return std::array<LibXR::Print::FormatArgumentInfo, 0>{};
  }

  [[nodiscard]] static constexpr auto ArgumentOrder() { return std::array<size_t, 0>{}; }

  [[nodiscard]] static constexpr LibXR::Print::FormatProfile Profile()
  {
    return LibXR::Print::FormatProfile::Generic;
  }
};

struct PrefixThenBrokenFormat
{
  template <typename... Args>
  [[nodiscard]] static consteval bool Matches()
  {
    return sizeof...(Args) == 0;
  }

  [[nodiscard]] static constexpr auto Codes()
  {
    return std::to_array<uint8_t>({
        static_cast<uint8_t>(LibXR::Print::FormatOp::TextInline),
        static_cast<uint8_t>('h'),
        static_cast<uint8_t>('e'),
        static_cast<uint8_t>('l'),
        static_cast<uint8_t>('l'),
        static_cast<uint8_t>('o'),
        static_cast<uint8_t>(' '),
        0,
        static_cast<uint8_t>(LibXR::Print::FormatOp::GenericField),
        static_cast<uint8_t>(LibXR::Print::FormatType::End),
        0,
        static_cast<uint8_t>(' '),
        0,
        0xFF,
        static_cast<uint8_t>(LibXR::Print::FormatOp::End),
    });
  }

  [[nodiscard]] static constexpr auto ArgumentList()
  {
    return std::array<LibXR::Print::FormatArgumentInfo, 0>{};
  }

  [[nodiscard]] static constexpr auto ArgumentOrder() { return std::array<size_t, 0>{}; }

  [[nodiscard]] static constexpr LibXR::Print::FormatProfile Profile()
  {
    return LibXR::Print::FormatProfile::Generic;
  }
};

template <LibXR::Print::Text Source, typename... Args>
bool SameAsSnprintf(Args... args)
{
  std::array<char, 1024> expected{};
  int expected_size = 0;
  if constexpr (sizeof...(Args) == 0)
  {
    expected_size = std::snprintf(expected.data(), expected.size(), "%s", Source.Data());
  }
  else
  {
    expected_size =
        std::snprintf(expected.data(), expected.size(), Source.Data(), args...);
  }
  if (expected_size < 0 || static_cast<size_t>(expected_size) >= expected.size())
  {
    return false;
  }

  StringSink sink;
  constexpr auto format = LibXR::Print::Printf::Build<Source>();
  auto ec = LibXR::Print::Write(sink, format, args...);
  if (ec != LibXR::ErrorCode::OK)
  {
    return false;
  }

  return sink.buffer ==
         std::string_view(expected.data(), static_cast<size_t>(expected_size));
}

template <LibXR::Print::Text Source, typename... Args>
bool SameFormatAsExpected(std::string_view expected, Args&&... args)
{
  StringSink sink;
  constexpr LibXR::Format<Source> format{};
  auto ec = format.WriteTo(sink, std::forward<Args>(args)...);
  if (ec != LibXR::ErrorCode::OK)
  {
    return false;
  }

  return sink.buffer == expected;
}

template <LibXR::Print::Text Source, typename... Args>
bool SamePrintfAsExpected(std::string_view expected, Args&&... args)
{
  StringSink sink;
  constexpr auto format = LibXR::Print::Printf::Build<Source>();
  auto ec = LibXR::Print::Write(sink, format, std::forward<Args>(args)...);
  if (ec != LibXR::ErrorCode::OK)
  {
    return false;
  }

  return sink.buffer == expected;
}

std::string PointerText(const void* value)
{
  std::array<char, 128> buffer{};
  int size = std::snprintf(buffer.data(), buffer.size(), "%p", value);
  if (size <= 0 || static_cast<size_t>(size) >= buffer.size())
  {
    return {};
  }
  return std::string(buffer.data(), static_cast<size_t>(size));
}

template <typename UInt>
std::string UnsignedBaseText(UInt value, uint8_t base, bool upper_case = false)
{
  static_assert(std::is_unsigned_v<UInt>);
  constexpr char lower_digits[] = "0123456789abcdef";
  constexpr char upper_digits[] = "0123456789ABCDEF";
  const char* digits = upper_case ? upper_digits : lower_digits;

  if (value == 0)
  {
    return "0";
  }

  std::string reversed;
  while (value != 0)
  {
    reversed.push_back(
        digits[static_cast<size_t>(value % static_cast<UInt>(base))]);
    value /= static_cast<UInt>(base);
  }

  return std::string(reversed.rbegin(), reversed.rend());
}

int Fail(const char* message)
{
  std::cerr << message << '\n';
  std::exit(1);
  return 0;
}

void TestPrintfFrontendSemantics()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  if (!SameAsSnprintf<"abc">())
  {
    Fail("plain text mismatch");
  }

  if (!SameAsSnprintf<"0123456789abcdef">())
  {
    Fail("long plain text mismatch");
  }

  if (!SameAsSnprintf<"%d|%+d|% d|%05d|%-5d|%.0d">(-7, 7, 7, 7, 7, 0))
  {
    Fail("signed integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%+ d|% +d|%05d|%-05d|%+05d|% 05d">(7, 7, 7, 7, 7, 7))
  {
    Fail("integer flag precedence mismatch");
  }

  if (!SameAsSnprintf<"%05.3d|%5.0d|%5.0x|%#.0x">(7, 0, 0U, 0U))
  {
    Fail("integer precision precedence mismatch");
  }

  if (!SameAsSnprintf<"%u|%.0u|%5.3u|%-5u">(7U, 0U, 7U, 7U))
  {
    Fail("unsigned integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%#x|%#X|%#.0x|%08x|%#08x">(42U, 42U, 0U, 42U, 42U))
  {
    Fail("hex integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%o|%#o|%#.0o|%#.3o">(8U, 8U, 0U, 1U))
  {
    Fail("octal integer semantics mismatch");
  }

  if (!SamePrintfAsExpected<"%b|%B|%#b|%#B|%08b">("101|101|0b101|0B101|00000101", 5U, 5U,
                                                  5U, 5U, 5U))
  {
    Fail("binary integer semantics mismatch");
  }

  {
    unsigned long long max_value = std::numeric_limits<unsigned long long>::max();
    std::string binary = UnsignedBaseText(max_value, 2);
    std::string octal = UnsignedBaseText(max_value, 8);
    std::string hex_lower = UnsignedBaseText(max_value, 16);
    std::string hex_upper = UnsignedBaseText(max_value, 16, true);
    std::string expected = binary + "|0b" + binary + "|0" + octal + "|" + hex_lower +
                           "|" + hex_upper;
    if (!SamePrintfAsExpected<"%llb|%#llb|%#llo|%llx|%llX">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("64-bit integer base semantics mismatch");
    }
  }

  if (!SameAsSnprintf<"x=%+08d y=%-4s z=%#x %% %.2f">(-12, "ok", 42U, 1.25))
  {
    Fail("mixed format mismatch");
  }

  {
    int value = 7;
    if (!SameAsSnprintf<"%c %.3s %p">('A', "abcdef", &value))
    {
      Fail("character string pointer mismatch");
    }
  }

  if (!SameAsSnprintf<"%c|%3c|%-3c">('A', 'B', 'C'))
  {
    Fail("character semantics mismatch");
  }

  if (!SameAsSnprintf<"[%s]">("abc"))
  {
    Fail("string format mismatch");
  }

  if (!SameAsSnprintf<"%s|%.3s|%6.3s|%-6.3s">("abcdef", "abcdef", "abcdef", "abcdef"))
  {
    Fail("string semantics mismatch");
  }

  {
    long signed_value = -12;
    size_t unsigned_value = 34;
    long double float_value = 1.25L;
    if (!SameAsSnprintf<"%ld %zu %.2Lf">(signed_value, unsigned_value, float_value))
    {
      Fail("length-modifier format mismatch");
    }
  }

  {
    signed char tiny_signed = -3;
    unsigned char tiny_unsigned = 4;
    short short_signed = -5;
    unsigned short short_unsigned = 6;
    long long long_long_signed = -7;
    unsigned long long long_long_unsigned = 8;
    intmax_t max_signed = -9;
    uintmax_t max_unsigned = 10;
    ptrdiff_t ptrdiff_signed = -11;
    std::make_unsigned_t<ptrdiff_t> ptrdiff_unsigned = 12;
    if (!SameAsSnprintf<"%hhd %hhu %hd %hu %lld %llu %jd %ju %td %tu">(
            tiny_signed, tiny_unsigned, short_signed, short_unsigned, long_long_signed,
            long_long_unsigned, max_signed, max_unsigned, ptrdiff_signed,
            ptrdiff_unsigned))
    {
      Fail("integer length family mismatch");
    }
  }

  if (!SameAsSnprintf<"%Lg|%Le|%Lf">(1.25L, 1.25L, 1.25L))
  {
    Fail("long double family mismatch");
  }

  if (!SameAsSnprintf<"%2$u %1$s %2$#x">("ok", 7U))
  {
    Fail("positional argument semantics mismatch");
  }

  if (!SameAsSnprintf<"%F|%E|%G|%LG">(1.25, 1.25, 1.25, 2.25L))
  {
    Fail("uppercase float family mismatch");
  }

  if (!SameAsSnprintf<"%#.0f|%#.0e|%#.3g">(12.0, 12.0, 1.2))
  {
    Fail("float alternate form mismatch");
  }

  if (!SameAsSnprintf<"%010f|%+010f|% 010f">(1.25, 1.25, 1.25))
  {
    Fail("float zero padding mismatch");
  }

  if (!SameAsSnprintf<"%g|%g|%.0g|%#.0g">(1000000.0, 999999.0, 12.0, 12.0))
  {
    Fail("float general threshold mismatch");
  }

  if (!SameAsSnprintf<"%f|%e|%g">(-0.0, -0.0, -0.0))
  {
    Fail("negative zero float mismatch");
  }

  if (!SameAsSnprintf<"%f|%F|%e">(std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("float inf nan mismatch");
  }

  {
    int value = 0;
    if (!SameAsSnprintf<"a%d 0123456789abcdef %u %o %x %X %p %c %s %f %e %g %Lf %Le %Lg">(
            -1, 2U, 8U, 42U, 42U, &value, 'Q', "xy", 1.5, 1.5, 1.5, 2.25L, 2.25L, 2.25L))
    {
      Fail("full supported family mismatch");
    }
  }

  {
    std::string text = "hello";
    std::string_view view = "xy";
    if (!SamePrintfAsExpected<"[%s][%s]">("[hello][xy]", text, view))
    {
      Fail("printf string object mismatch");
    }
  }

  {
    char bounded_text[4] = {'i', 'm', 'u', '\0'};
    const char embedded_text[5] = {'a', 'b', '\0', 'c', 'd'};
    if (!SamePrintfAsExpected<"[%s][%s]">("[imu][ab]", bounded_text, embedded_text))
    {
      Fail("printf bounded char array mismatch");
    }
  }

  {
    enum PlainHex : unsigned
    {
      PLAIN_HEX = 42U
    };
    if (!SameAsSnprintf<"%#x|%u">(PLAIN_HEX, PLAIN_HEX))
    {
      Fail("printf enum mismatch");
    }
  }

  if (!SamePrintfAsExpected<"[%s]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("printf null string mismatch");
  }
}

void TestFormatFrontendSemantics()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  if (!SameFormatAsExpected<"abc">("abc"))
  {
    Fail("format frontend plain text mismatch");
  }

  if (!SameFormatAsExpected<"{{x}}={0} {1} {0}">("{x}=7 ok 7", 7, "ok"))
  {
    Fail("format frontend reorder and escapes mismatch");
  }

  if (!SameFormatAsExpected<"x={:+08d} y={:*^6s} z={:#x} {:.2f}">(
          "x=-0000012 y=**ok** z=0x2a 1.25", -12, "ok", 42U, 1.25))
  {
    Fail("format frontend mixed mismatch");
  }

  if (!SameFormatAsExpected<"{:#b} {:#B} {:#o}">("0b101 0B101 010", 5U, 5U, 8U))
  {
    Fail("format frontend non-decimal mismatch");
  }

  {
    uint64_t max_value = std::numeric_limits<uint64_t>::max();
    std::string binary = UnsignedBaseText(max_value, 2);
    std::string octal = UnsignedBaseText(max_value, 8);
    std::string hex_lower = UnsignedBaseText(max_value, 16);
    std::string hex_upper = UnsignedBaseText(max_value, 16, true);
    std::string expected = binary + "|0b" + binary + "|0" + octal + "|" + hex_lower +
                           "|" + hex_upper;
    if (!SameFormatAsExpected<"{:b}|{:#b}|{:#o}|{:x}|{:X}">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("format frontend 64-bit base mismatch");
    }
  }

  if (!SameFormatAsExpected<"[{:.3s}] [{:_>6s}] [{:*^7s}]">("[abc] [___abc] [**abc**]",
                                                            "abcdef", "abc", "abc"))
  {
    Fail("format frontend string field mismatch");
  }

  if (!SameFormatAsExpected<"{:c} {:c}">("A B", 'A', 66))
  {
    Fail("format frontend character mismatch");
  }

  if (!SameFormatAsExpected<"{:.2f}|{:.1E}|{:g}">("1.25|1.2E+01|12", 1.25, 12.0, 12.0))
  {
    Fail("format frontend float mismatch");
  }

  if (!SameFormatAsExpected<"{:+d}|{: d}|{:08d}|{:#x}">("+7| 7|00000007|0x2a", 7, 7, 7,
                                                        42U))
  {
    Fail("format frontend integer flag mismatch");
  }

  if (!SameFormatAsExpected<"{1:+08d} {0:#x} {1}">("-0000012 0x2a -12", 42U, -12))
  {
    Fail("format frontend indexed spec mismatch");
  }

  if (!SameFormatAsExpected<"{:f}|{:E}|{:g}">("-0.000000|-0.000000E+00|-0", -0.0, -0.0,
                                              -0.0))
  {
    Fail("format frontend negative zero mismatch");
  }

  if (!SameFormatAsExpected<"{:f}|{:F}|{:e}">("inf|-INF|nan",
                                              std::numeric_limits<double>::infinity(),
                                              -std::numeric_limits<double>::infinity(),
                                              std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("format frontend inf nan mismatch");
  }

  if (!SameFormatAsExpected<"[{:3}] [{:6}]">("[A  ] [abc   ]", 'A', "abc"))
  {
    Fail("format frontend default text alignment mismatch");
  }

  {
    std::string text = "hello";
    std::string_view view = "xy";
    if (!SameFormatAsExpected<"[{}][{}]">("[hello][xy]", text, view))
    {
      Fail("format frontend string object mismatch");
    }
  }

  {
    char bounded_text[4] = {'i', 'm', 'u', '\0'};
    const char embedded_text[5] = {'a', 'b', '\0', 'c', 'd'};
    if (!SameFormatAsExpected<"[{}][{}]">("[imu][ab]", bounded_text, embedded_text))
    {
      Fail("format frontend bounded char array mismatch");
    }
  }

  if (!SameFormatAsExpected<"[{}]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("format frontend null string mismatch");
  }

  {
    constexpr LibXR::Format<"{:c} {:.3s} {:p}"> format{};
    int value = 7;
    StringSink sink;
    auto ec = format.WriteTo(sink, 'A', "abcdef", &value);
    if (ec != LibXR::ErrorCode::OK || !sink.buffer.starts_with("A abc 0x"))
    {
      Fail("format frontend writeto mismatch");
    }
  }

  {
    int value = 0;
    std::string expected = "ptr=" + PointerText(&value);
    if (!SameFormatAsExpected<"ptr={}">(expected, &value))
    {
      Fail("format frontend pointer default mismatch");
    }
  }

  {
    int value = 0;
    std::string pointer = PointerText(&value);
    std::string expected = "[";
    if (pointer.size() < 32)
    {
      expected.append(32 - pointer.size(), ' ');
    }
    expected += pointer;
    expected.push_back(']');
    if (!SameFormatAsExpected<"[{:32}]">(expected, &value))
    {
      Fail("format frontend pointer default alignment mismatch");
    }
  }
}

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

void TestStreamBackedPrintFailureKeepsPrefix()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  static constexpr char expected[] = "hello ";

  LibXR::Pipe pipe(64);
  LibXR::ReadPort& read = pipe.GetReadPort();
  LibXR::WritePort& write = pipe.GetWritePort();

  uint8_t rx[sizeof(expected) - 1] = {0};
  LibXR::ReadOperation read_op;
  if (read(LibXR::RawData{rx, sizeof(rx)}, read_op) != LibXR::ErrorCode::OK)
  {
    Fail("stream-backed print failure read arm failed");
  }

  LibXR::WriteOperation write_op;
  LibXR::WritePort::Stream stream(&write, write_op);
  auto ec = LibXR::Print::Write(stream, PrefixThenBrokenFormat{});
  if (ec != LibXR::ErrorCode::STATE_ERR)
  {
    Fail("stream-backed print failure status mismatch");
  }

  if (stream.Commit() != LibXR::ErrorCode::OK)
  {
    Fail("stream-backed print failure commit mismatch");
  }

  read.ProcessPendingReads(false);
  if (std::memcmp(rx, expected, sizeof(rx)) != 0)
  {
    Fail("stream-backed print failure prefix mismatch");
  }
}

}  // namespace

void test_print()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestPrintfFrontendSemantics();
  TestFormatFrontendSemantics();
  TestPrintApiWrappers();
  TestStreamBackedPrintFailureKeepsPrefix();
}
