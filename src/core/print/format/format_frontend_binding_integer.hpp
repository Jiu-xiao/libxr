#pragma once

/**
 * @brief brace 前端里处理 bool、整数、字符、字符串与指针参数字段的辅助函数 / Brace-frontend helpers for fields that point to bool, integer, character, string, and pointer arguments
 */
namespace ArgumentResolution
{

/**
 * @brief 针对整数类参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for an integer-like argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @param signed_decimal 选中的参数是否按有符号十进制族处理 / Whether the selected argument should be treated as a signed decimal family
 * @param uses_64bit_storage 选中的参数是否需要 64 位打包存储 / Whether the selected argument requires 64-bit packed storage
 * @return 返回解析后的共享字段；不匹配时返回首个类型错误 / Returns the resolved shared field, or the first type-mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolveIntegerField(const ParsedField& parsed,
                                                          bool signed_decimal,
                                                          bool uses_64bit_storage)
{
  if (!Config::enable_integer)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (uses_64bit_storage && !Config::enable_integer_64bit)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  if (parsed.has_precision)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  if (parsed.presentation == 'c')
  {
    if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad)
    {
      return ResolvedField{.error = Error::ArgumentTypeMismatch};
    }
    return ResolvedField{.field = MakeField(parsed, FormatType::Character,
                                            FormatPackKind::Character)};
  }

  if (IsDefaultOr(parsed.presentation, 'd'))
  {
    if (parsed.alternate)
    {
      return ResolvedField{.error = Error::ArgumentTypeMismatch};
    }
    if (!signed_decimal && HasSignOption(parsed))
    {
      return ResolvedField{.error = Error::ArgumentTypeMismatch};
    }

    if (signed_decimal)
    {
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Signed64
                                                 : FormatType::Signed32,
                              uses_64bit_storage ? FormatPackKind::I64
                                                 : FormatPackKind::I32)};
    }

    return ResolvedField{.field = MakeField(
                            parsed,
                            uses_64bit_storage ? FormatType::Unsigned64
                                               : FormatType::Unsigned32,
                            uses_64bit_storage ? FormatPackKind::U64
                                               : FormatPackKind::U32)};
  }

  if (!Config::enable_integer_base8_16 || HasSignOption(parsed))
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  switch (parsed.presentation)
  {
    case 'b':
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Binary64
                                                 : FormatType::Binary32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'B':
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Binary64
                                                 : FormatType::Binary32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32,
                              true)};
    case 'o':
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Octal64
                                                 : FormatType::Octal32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'x':
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::HexLower64
                                                 : FormatType::HexLower32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'X':
      return ResolvedField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::HexUpper64
                                                 : FormatType::HexUpper32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32,
                              true)};
    default:
      return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
}

/**
 * @brief 针对 bool 参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for a bool argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @return 返回解析后的共享字段；不匹配时返回首个类型错误 / Returns the resolved shared field, or the first type-mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolveBoolField(const ParsedField& parsed)
{
  if (parsed.presentation == 0)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  return ResolveIntegerField(parsed, false, false);
}

/**
 * @brief 针对字符参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for a character argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @return 返回解析后的共享字段；不匹配时返回首个类型错误 / Returns the resolved shared field, or the first type-mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolveCharacterField(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 'c')
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad || parsed.has_precision)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_text)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  ParsedField adjusted = parsed;
  if (adjusted.align == Align::None)
  {
    adjusted.align = Align::Left;
  }

  return ResolvedField{
      .field = MakeField(adjusted, FormatType::Character, FormatPackKind::Character)};
}

/**
 * @brief 针对字符串类参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for a string-like argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @return 返回解析后的共享字段；不匹配时返回首个类型错误 / Returns the resolved shared field, or the first type-mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolveStringField(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 's')
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_text)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  ParsedField adjusted = parsed;
  if (adjusted.align == Align::None)
  {
    adjusted.align = Align::Left;
  }

  return ResolvedField{
      .field = MakeField(adjusted, FormatType::String, FormatPackKind::StringView)};
}

/**
 * @brief 针对指针类参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for a pointer-like argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @return 返回解析后的共享字段；不匹配时返回首个类型错误 / Returns the resolved shared field, or the first type-mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolvePointerField(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 'p')
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad || parsed.has_precision)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_pointer)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  return ResolvedField{
      .field = MakeField(parsed, FormatType::Pointer, FormatPackKind::Pointer)};
}
}  // namespace ArgumentResolution
