/**
 * @file print_sink_test_common.hpp
 * @brief `print` 测试用 sink 和坏格式探针。 Test sinks and malformed-format probes
 * for `print` tests.
 * @details
 * 1. `StringSink` 收集完整输出，`LimitedSink` 模拟 sink 写满。
 * 2. `BrokenGenericFormat` 测 wrapper 错误返回。
 * 3. `PrefixThenBrokenFormat` 测 stream 前缀保留。
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "print_literal_test_common.hpp"

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
}  // namespace LibXRPrintTest
