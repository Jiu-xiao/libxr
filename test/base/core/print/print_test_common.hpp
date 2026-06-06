/**
 * @file print_test_common.hpp
 * @brief `print` 基础测试共用 helper 与断言工具。 Shared helpers and assertions for base `print` tests.
 *
 * 作用 / Purpose:
 * 1. 收口 `print` 前端测试共用的 sink、坏格式对象和字符串对照 helper。
 *    Centralize shared sinks, broken-format probes, and string-comparison helpers used by `print` frontend tests.
 * 2. 让各个独立测试项文件只保留自己的语义场景，而不是重复粘贴基础工具。
 *    Keep each split test file focused on its own semantic scenario instead of duplicating utility code.
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
/**
 * @brief 辅助函数 `SameAsSnprintf`。 Helper function `SameAsSnprintf`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
/**
 * @brief 辅助函数 `SameFormatAsExpected`。 Helper function `SameFormatAsExpected`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
/**
 * @brief 辅助函数 `SamePrintfAsExpected`。 Helper function `SamePrintfAsExpected`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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

/**
 * @brief 辅助函数 `PointerText`。 Helper function `PointerText`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
/**
 * @brief 辅助函数 `UnsignedBaseText`。 Helper function `UnsignedBaseText`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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

/**
 * @brief 辅助函数 `Fail`。 Helper function `Fail`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
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
