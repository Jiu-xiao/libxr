/**
 * @file print_literal_test_common.hpp
 * @brief `print` 字面量前端的编译期约束。 Compile-time constraints for `print`
 * literal frontends.
 * @details
 * 1. 固化 brace `Format` 的参数匹配规则。
 * 2. 固化 logger 自动选择 brace/printf/None/Ambiguous 的边界。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

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

// 裁剪边界：常见裸整数/文本字段不能退回 GenericField。
// 否则 float 默认开启时会留下浮点分发后端。
// Trimming boundary: raw integer/text fields must not use GenericField.
// Otherwise the default float-enabled profile retains float dispatch.
static_assert(
    !HasProfileBit<PrintfRawIntegerFormat>(LibXR::Print::FormatProfile::Generic));
static_assert(HasProfileBit<PrintfRawIntegerFormat>(LibXR::Print::FormatProfile::NarrowInt));
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
static_assert(HasProfileBit<FormatRawIntegerFormat>(LibXR::Print::FormatProfile::NarrowInt));
static_assert(
    HasProfileBit<FormatRawIntegerFormat>(LibXR::Print::FormatProfile::TextArg));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::I32Dec));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::U32HexLower));
static_assert(HasOp<FormatRawIntegerFormat>(LibXR::Print::FormatOp::CharacterRaw));
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
