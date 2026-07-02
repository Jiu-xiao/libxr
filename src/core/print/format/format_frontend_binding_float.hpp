#pragma once

/**
 * @brief brace 前端里处理浮点参数字段的辅助函数 / Brace-frontend helpers for fields that point to float arguments
 */
namespace ArgumentResolution
{

/**
 * @brief 针对 float、double 或 long double 参数解析一个已解析的 brace 字段 / Resolve one parsed brace field for a float, double, or long double argument
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @param kind 当前字段选中的前端参数类别 / Frontend-side argument family selected for this field
 * @return 返回解析后的共享字段；精度或类型不匹配时返回首个错误 / Returns the resolved shared field, or the first precision or type mismatch error
 */
[[nodiscard]] consteval ResolvedField ResolveFloatField(const ParsedField& parsed,
                                                        ArgumentKind kind)
{
  if (parsed.has_precision && parsed.precision > Config::max_float_precision)
  {
    return ResolvedField{.error = Error::FloatPrecisionLimitExceeded};
  }

  char presentation =
      parsed.presentation == 0 ? DefaultFloatPresentation() : parsed.presentation;
  if (presentation == 0)
  {
    return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }
  bool upper_case =
      presentation == 'F' || presentation == 'E' || presentation == 'G';

  FormatType type = FormatType::End;
  FormatPackKind pack = FormatPackKind::F32;

  auto pick_type = [&](FormatType f32_type, FormatType f64_type,
                       FormatType ld_type) consteval -> bool {
    switch (kind)
    {
      case ArgumentKind::Float32:
        type = f32_type;
        pack = FormatPackKind::F32;
        return true;
      case ArgumentKind::Float64:
        // When enable_float_double is false, a double argument is silently
        // formatted at float precision (7 significant digits). If full double
        // precision is needed, set Config::enable_float_double = true, or
        // cast the argument to float explicitly to make the demotion visible.
        type = Config::enable_float_double ? f64_type : f32_type;
        pack = Config::enable_float_double ? FormatPackKind::F64 : FormatPackKind::F32;
        return true;
      case ArgumentKind::LongDouble:
        if (!Config::enable_float_long_double)
        {
          return false;
        }
        type = ld_type;
        pack = FormatPackKind::LongDouble;
        return true;
      default:
        return false;
    }
  };

  switch (presentation)
  {
    case 'f':
    case 'F':
      if (!Config::enable_float_fixed ||
          !pick_type(FormatType::FloatFixed, FormatType::DoubleFixed,
                     FormatType::LongDoubleFixed))
      {
        return ResolvedField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    case 'e':
    case 'E':
      if (!Config::enable_float_scientific ||
          !pick_type(FormatType::FloatScientific, FormatType::DoubleScientific,
                     FormatType::LongDoubleScientific))
      {
        return ResolvedField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    case 'g':
    case 'G':
      if (!Config::enable_float_general ||
          !pick_type(FormatType::FloatGeneral, FormatType::DoubleGeneral,
                     FormatType::LongDoubleGeneral))
      {
        return ResolvedField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    default:
      return ResolvedField{.error = Error::ArgumentTypeMismatch};
  }

  return ResolvedField{.field = MakeField(parsed, type, pack, upper_case)};
}

/**
 * @brief 先判断一个已解析 brace 字段指向哪类参数，再选择匹配的字段构造逻辑 / Check which argument family a parsed brace field points to, then choose the matching field builder
 * @tparam Args 已绑定的 C++ 实参类型列表 / Bound C++ argument types
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @return 返回解析后的共享字段；缺参或类型不支持时返回首个错误 / Returns the resolved shared field, or the first missing-argument or unsupported-type error
 */
template <typename... Args>
[[nodiscard]] consteval ResolvedField ResolveField(const ParsedField& parsed)
{
  constexpr auto argument_summaries = ClassifyArguments<Args...>();
  if (parsed.arg_index >= argument_summaries.size())
  {
    return ResolvedField{.error = Error::MissingArgument};
  }

  auto argument = argument_summaries[parsed.arg_index];
  switch (argument.kind)
  {
    case ArgumentKind::Bool:
      return ResolveBoolField(parsed);
    case ArgumentKind::Character:
      return ResolveCharacterField(parsed);
    case ArgumentKind::Signed:
      return ResolveIntegerField(parsed, true, argument.uses_64bit_storage);
    case ArgumentKind::Unsigned:
      return ResolveIntegerField(parsed, false, argument.uses_64bit_storage);
    case ArgumentKind::String:
      return ResolveStringField(parsed);
    case ArgumentKind::Pointer:
      return ResolvePointerField(parsed);
    case ArgumentKind::Float32:
    case ArgumentKind::Float64:
    case ArgumentKind::LongDouble:
      return ResolveFloatField(parsed, argument.kind);
    case ArgumentKind::Unsupported:
    default:
      return ResolvedField{.error = Error::UnsupportedArgumentType};
  }
}
}  // namespace ArgumentResolution
