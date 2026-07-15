/**
 * @file print_compare_test_common.hpp
 * @brief `print` 输出对照和失败报告 helper。 Output comparison and failure-report helpers
 * for `print` tests.
 * @details
 * 1. `SameAsSnprintf` 对照 host `snprintf`。
 * 2. Expected-text helpers cover brace formats.
 * 3. Expected-text helpers also cover LibXR extensions.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "print_sink_test_common.hpp"

namespace LibXRPrintTest
{
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
