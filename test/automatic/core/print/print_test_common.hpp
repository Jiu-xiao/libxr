/**
 * @file print_test_common.hpp
 * @brief 打印测试共用的输出接收器、格式比较和失败格式。 /
 * Shared output sinks, format comparisons and malformed formats for print tests.
 *
 * 另在编译期检查参数匹配，以及常用整数和文本字段生成的指令。
 * Also checks argument matching and generated integer and text operations at compile
 * time.
 */

#pragma once

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

namespace LibXRPrintTest::CompileProfile
{
template <typename Format>
[[nodiscard]] consteval bool HasOp(LibXR::Print::FormatOp op)
{
  auto codes = Format::Codes();
  size_t pos = 0;
  while (pos < codes.size())
  {
    auto current = static_cast<LibXR::Print::FormatOp>(codes[pos++]);
    if (current == op)
    {
      return true;
    }

    const size_t payload_bytes = LibXR::Print::FormatOpPayloadBytes(current);
    if (current == LibXR::Print::FormatOp::TextInline)
    {
      while (pos < codes.size() && codes[pos++] != 0)
      {
      }
    }
    else if (current == LibXR::Print::FormatOp::End)
    {
      return false;
    }
    else if (payload_bytes != 0)
    {
      pos += payload_bytes;
    }
    else if (current != LibXR::Print::FormatOp::TextSpace &&
             current != LibXR::Print::FormatOp::U32Dec &&
             current != LibXR::Print::FormatOp::I32Dec &&
             current != LibXR::Print::FormatOp::U32Binary &&
             current != LibXR::Print::FormatOp::U32Octal &&
             current != LibXR::Print::FormatOp::U32HexLower &&
             current != LibXR::Print::FormatOp::U32HexUpper &&
             current != LibXR::Print::FormatOp::StringRaw &&
             current != LibXR::Print::FormatOp::CharacterRaw)
    {
      return false;
    }
  }
  return false;
}

template <typename Format>
[[nodiscard]] consteval bool HasProfileBit(LibXR::Print::FormatProfile bit)
{
  return LibXR::Print::HasProfile(Format::Profile(), bit);
}

using PrintfRawIntegerFormat =
    decltype(LibXR::Print::Printf::Build<"%d %u %b %o %x %X">());
using PrintfIntTextFormat =
    decltype(LibXR::Print::Printf::Build<"id=%d hex=%x msg=%s ch=%c">());
using FormatRawIntegerFormat =
    LibXR::Format<"{} {:x} {:c}">::Compiled<int, unsigned, int>;
using FormatGenericIntegerFormat = LibXR::Format<"{:5d}">::Compiled<int>;
using FormatGenericTextFormat = LibXR::Format<"{:.2s}">::Compiled<const char*>;
using FormatGenericFloatFormat = LibXR::Format<"{:5.2f}">::Compiled<float>;

// 裁剪边界：常见裸整数/文本字段不能退回 GenericField。
// 否则 float 默认开启时会留下浮点分发后端。
// Trimming boundary: raw integer/text fields must not use GenericField.
// Otherwise the default float-enabled profile retains float dispatch.
static_assert(
    !HasProfileBit<PrintfRawIntegerFormat>(LibXR::Print::FormatProfile::Generic));
static_assert(
    HasProfileBit<PrintfRawIntegerFormat>(LibXR::Print::FormatProfile::NarrowInt));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::I32Dec));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::U32Dec));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::U32Binary));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::U32Octal));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::U32HexLower));
static_assert(HasOp<PrintfRawIntegerFormat>(LibXR::Print::FormatOp::U32HexUpper));

static_assert(!HasProfileBit<PrintfIntTextFormat>(LibXR::Print::FormatProfile::Generic));
static_assert(HasProfileBit<PrintfIntTextFormat>(LibXR::Print::FormatProfile::NarrowInt));
static_assert(HasProfileBit<PrintfIntTextFormat>(LibXR::Print::FormatProfile::TextArg));
static_assert(HasOp<PrintfIntTextFormat>(LibXR::Print::FormatOp::I32Dec));
static_assert(HasOp<PrintfIntTextFormat>(LibXR::Print::FormatOp::U32HexLower));
static_assert(HasOp<PrintfIntTextFormat>(LibXR::Print::FormatOp::StringRaw));
static_assert(HasOp<PrintfIntTextFormat>(LibXR::Print::FormatOp::CharacterRaw));

static_assert(
    !HasProfileBit<FormatRawIntegerFormat>(LibXR::Print::FormatProfile::Generic));
static_assert(
    HasProfileBit<FormatRawIntegerFormat>(LibXR::Print::FormatProfile::NarrowInt));
static_assert(
    HasProfileBit<FormatRawIntegerFormat>(LibXR::Print::FormatProfile::TextArg));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::I32Dec));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::U32HexLower));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::CharacterRaw));

static_assert(HasProfileBit<FormatGenericIntegerFormat>(
    LibXR::Print::FormatProfile::GenericSigned32));
static_assert(!HasProfileBit<FormatGenericIntegerFormat>(
    LibXR::Print::FormatProfile::GenericString));
static_assert(!HasProfileBit<FormatGenericIntegerFormat>(
    LibXR::Print::FormatProfile::GenericFloatFixed));
static_assert(
    HasProfileBit<FormatGenericTextFormat>(LibXR::Print::FormatProfile::GenericString));
static_assert(!HasProfileBit<FormatGenericTextFormat>(
    LibXR::Print::FormatProfile::GenericSigned32));
static_assert(!HasProfileBit<FormatGenericTextFormat>(
    LibXR::Print::FormatProfile::GenericFloatFixed));
static_assert(HasProfileBit<FormatGenericFloatFormat>(
    LibXR::Print::FormatProfile::GenericFloatFixed));
static_assert(!HasProfileBit<FormatGenericFloatFormat>(
    LibXR::Print::FormatProfile::GenericSigned32));
static_assert(
    !HasProfileBit<FormatGenericFloatFormat>(LibXR::Print::FormatProfile::GenericString));
}  // namespace LibXRPrintTest::CompileProfile

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

namespace LibXRPrintTest
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

inline std::string PointerText(const void* value)
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
    reversed.push_back(digits[static_cast<size_t>(value % static_cast<UInt>(base))]);
    value /= static_cast<UInt>(base);
  }

  return std::string(reversed.rbegin(), reversed.rend());
}

inline int Fail(const char* message)
{
  std::cerr << message << '\n';
  std::exit(1);
  return 0;
}

void TestPrintfFrontendSemantics();
void TestFormatFrontendSemantics();
void TestPrintApiWrappers();
void TestStreamBackedPrintFailureKeepsPrefix();
}  // namespace LibXRPrintTest
