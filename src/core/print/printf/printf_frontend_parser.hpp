#pragma once

#include "printf_frontend_parse_util.hpp"

/**
 * @brief 单个 printf 转换体的源级解析器 / Source parser for one printf conversion body
 */

/**
 * @brief 解析单个转换项前导的标志位簇 / Parse the leading flag cluster of one conversion
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置，成功时推进到已解析标志之后 / Current parse position; advanced past parsed flags
 * @param conversion 当前正在填充的转换项 / Conversion being filled
 * @return 成功返回 `Error::None`；遇到非法标志时返回对应错误 / Returns `Error::None` on success, or the first invalid flag error
 */
[[nodiscard]] consteval Error ParseFlags(std::string_view source, size_t& pos,
                                         Conversion& conversion)
{
  while (pos < source.size())
  {
    switch (source[pos])
    {
      case '-':
        conversion.left_align = true;
        break;
      case '+':
        conversion.force_sign = true;
        break;
      case ' ':
        conversion.space_sign = true;
        break;
      case '#':
        if (!Config::enable_alternate)
        {
          return Error::InvalidSpecifier;
        }
        conversion.alternate = true;
        break;
      case '0':
        conversion.zero_pad = true;
        break;
      default:
        return Error::None;
    }
    ++pos;
  }

  return Error::None;
}

/**
 * @brief 解析一个可选的常量宽度字段 / Parse one optional constant width field
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置，成功时推进到宽度片段之后 / Current parse position; advanced past the width fragment on success
 * @param conversion 当前正在填充的转换项 / Conversion being filled
 * @return 成功返回 `Error::None`；动态宽度或非法宽度时返回对应错误 / Returns `Error::None` on success, or the first dynamic-field or invalid-width error
 */
[[nodiscard]] consteval Error ParseWidth(std::string_view source, size_t& pos,
                                         Conversion& conversion)
{
  if (pos < source.size() && source[pos] == '*')
  {
    return Error::DynamicField;
  }

  if (!Config::enable_width && pos < source.size() && IsDigit(source[pos]))
  {
    return Error::InvalidSpecifier;
  }

  return ParseByte(source, pos, std::numeric_limits<uint8_t>::max(),
                   conversion.width);
}

/**
 * @brief 解析一个可选的常量精度字段 / Parse one optional constant precision field
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置，成功时推进到精度片段之后 / Current parse position; advanced past the precision fragment on success
 * @param conversion 当前正在填充的转换项 / Conversion being filled
 * @return 成功返回 `Error::None`；精度相关错误时返回对应错误 / Returns `Error::None` on success, or the first precision-related error
 */
[[nodiscard]] consteval Error ParsePrecision(std::string_view source, size_t& pos,
                                             Conversion& conversion)
{
  if (pos >= source.size() || source[pos] != '.')
  {
    return Error::None;
  }

  if (!Config::enable_precision)
  {
    return Error::InvalidSpecifier;
  }

  ++pos;
  if (pos < source.size() && source[pos] == '*')
  {
    return Error::DynamicField;
  }

  if (pos < source.size() && IsDigit(source[pos]))
  {
    conversion.has_precision = true;
    return ParseByte(
        source, pos, static_cast<uint8_t>(std::numeric_limits<uint8_t>::max() - 1),
        conversion.precision);
  }

  conversion.has_precision = true;
  conversion.precision = 0;
  return Error::None;
}

/**
 * @brief 解析一个可选的长度修饰符序列 / Parse one optional length modifier sequence
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置；若存在长度修饰符则推进到其后 / Current parse position; advanced past the length modifier when present
 * @param conversion 当前正在填充的转换项 / Conversion being filled
 */
consteval void ParseLength(std::string_view source, size_t& pos,
                           Conversion& conversion)
{
  if (pos >= source.size())
  {
    return;
  }

  char token = source[pos];
  if (token == 'h' || token == 'l')
  {
    ++pos;
    conversion.length = (token == 'h') ? Length::Short : Length::Long;
    if (pos < source.size() && source[pos] == token)
    {
      conversion.length = (token == 'h') ? Length::Char : Length::LongLong;
      ++pos;
    }
    return;
  }

  switch (token)
  {
    case 'j':
      conversion.length = Length::IntMax;
      ++pos;
      return;
    case 'z':
      conversion.length = Length::Size;
      ++pos;
      return;
    case 't':
      conversion.length = Length::PtrDiff;
      ++pos;
      return;
    case 'L':
      conversion.length = Length::LongDouble;
      ++pos;
      return;
    default:
      return;
  }
}

/**
 * @brief 解析并校验最终的转换说明符字符 / Parse and validate the final conversion specifier token
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置，成功时推进到说明符之后 / Current parse position; advanced past the specifier on success
 * @param conversion 当前正在填充的转换项 / Conversion being filled
 * @return 成功返回 `Error::None`；说明符、长度或功能开关不合法时返回对应错误 / Returns `Error::None` on success, or the first specifier, length, or gate error
 */
[[nodiscard]] consteval Error ParseSpecifier(std::string_view source, size_t& pos,
                                             Conversion& conversion)
{
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  auto descriptor = FieldSelection::LookupSpecifier(source[pos]);
  if (descriptor.type == ValueKind::None)
  {
    return Error::InvalidSpecifier;
  }
  if (!FieldSelection::LengthAllowed(descriptor.length_policy, conversion.length))
  {
    return Error::InvalidLength;
  }
  if (!FieldSelection::TypeEnabled(descriptor.gate, conversion.length))
  {
    return Error::InvalidSpecifier;
  }

  conversion.type = descriptor.type;
  conversion.upper_case = conversion.upper_case || descriptor.upper_case;
  ++pos;
  return Error::None;
}

/**
 * @brief 在前导 `%` 之后解析一个完整 printf 转换项 / Parse one complete printf conversion after the leading `%`
 * @param source 完整 printf 源字符串 / Full printf source string
 * @param pos 当前解析位置，进入时应指向 `%`，成功时推进到该转换项之后 / Current parse position; must point at `%` on entry and lands after the conversion on success
 * @param indexing 源级索引模式跟踪状态 / Source-level indexing mode tracker
 * @param conversion 输出转换项结果 / Conversion result output
 * @return 成功返回 `Error::None`；解析或校验失败时返回对应错误 / Returns `Error::None` on success, or the first parse or validation failure
 */
[[nodiscard]] consteval Error Parse(std::string_view source, size_t& pos,
                                    IndexingState& indexing,
                                    Conversion& conversion)
{
  ++pos;
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  auto error = ParseArgumentIndex(source, pos, indexing, conversion);
  if (error != Error::None)
  {
    return error;
  }

  error = ParseFlags(source, pos, conversion);
  if (error != Error::None)
  {
    return error;
  }

  error = ParseWidth(source, pos, conversion);
  if (error != Error::None)
  {
    return error;
  }

  error = ParsePrecision(source, pos, conversion);
  if (error != Error::None)
  {
    return error;
  }

  ParseLength(source, pos, conversion);
  error = ParseSpecifier(source, pos, conversion);
  if (error != Error::None)
  {
    return error;
  }

  error = FieldSelection::ValidateConversion(conversion);
  if (error != Error::None)
  {
    return error;
  }

  if (!conversion.positional)
  {
    if (indexing.uses_positional)
    {
      return Error::MixedIndexing;
    }

    indexing.uses_sequential = true;
    conversion.arg_index = indexing.next_index++;
  }

  return Error::None;
}
