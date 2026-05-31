#pragma once

/**
 * @brief brace 前端共享的参数分类与字段形状构造辅助函数 / Shared brace-frontend argument classification and field-shape helpers
 */
namespace ArgumentResolution
{

/**
 * @brief 将一个 C++ 参数类型归类到 brace 前端使用的本地参数类别 / Classify one C++ argument type into the brace frontend's local category
 * @tparam Arg 待归类的 C++ 参数类型 / C++ argument type to classify
 * @return 返回后续字段解析要使用的本地前端参数摘要 / Returns the local frontend summary used by later field resolution
 */
template <typename Arg>
[[nodiscard]] consteval ArgumentSummary ClassifyArgument()
{
  using Traits = FormatArgument::TypeTraits<Arg>;
  using Decayed = typename Traits::Decayed;
  using Normalized = typename Traits::Normalized;

  if constexpr (std::is_same_v<Decayed, bool>)
  {
    return ArgumentSummary{.kind = ArgumentKind::Bool};
  }
  else if constexpr (std::is_same_v<Decayed, char>)
  {
    return ArgumentSummary{.kind = ArgumentKind::Character};
  }
  else if constexpr (Traits::is_string_like)
  {
    return ArgumentSummary{.kind = ArgumentKind::String};
  }
  else if constexpr (Traits::is_pointer_like)
  {
    return ArgumentSummary{.kind = ArgumentKind::Pointer};
  }
  else if constexpr (Traits::is_signed_integer)
  {
    return ArgumentSummary{
        .kind = ArgumentKind::Signed,
        .uses_64bit_storage = sizeof(Normalized) > sizeof(int32_t),
    };
  }
  else if constexpr (Traits::is_unsigned_integer)
  {
    return ArgumentSummary{
        .kind = ArgumentKind::Unsigned,
        .uses_64bit_storage = sizeof(Normalized) > sizeof(uint32_t),
    };
  }
  else if constexpr (std::is_same_v<Decayed, float>)
  {
    return ArgumentSummary{.kind = ArgumentKind::Float32};
  }
  else if constexpr (std::is_same_v<Decayed, double>)
  {
    return ArgumentSummary{.kind = ArgumentKind::Float64};
  }
  else if constexpr (std::is_same_v<Decayed, long double>)
  {
    return ArgumentSummary{.kind = ArgumentKind::LongDouble};
  }
  else
  {
    return ArgumentSummary{};
  }
}

/**
 * @brief 为 brace 前端归类整组 C++ 参数类型 / Classify one full C++ argument list for the brace frontend
 * @tparam Args 待归类的 C++ 实参类型列表 / C++ argument types to classify
 * @return 按源参数顺序返回每个参数对应的一份 `ArgumentSummary` / Returns one `ArgumentSummary` per argument in source order
 */
template <typename... Args>
[[nodiscard]] consteval auto ClassifyArguments()
{
  return std::array<ArgumentSummary, sizeof...(Args)>{ClassifyArgument<Args>()...};
}

/**
 * @brief 判断某个已解析字段是否请求了显式符号策略 / Return whether one parsed field requested an explicit sign policy
 * @param field 待检查的已解析字段 / Parsed field to inspect
 * @return 若请求了 `+` 或空格符号策略则返回 `true`，否则返回 `false` / Returns `true` when `+` or space-sign was requested, otherwise `false`
 */
[[nodiscard]] constexpr bool HasSignOption(const ParsedField& field)
{
  return field.force_sign || field.space_sign;
}

/**
 * @brief 返回“未指定精度”的本地哨兵值 / Return the local sentinel meaning "precision not specified"
 * @return 返回 brace 前端内部使用的精度哨兵值 / Returns the precision sentinel byte used inside the brace frontend
 */
[[nodiscard]] constexpr uint8_t UnspecifiedPrecision()
{
  return std::numeric_limits<uint8_t>::max();
}

/**
 * @brief 根据一个已解析 brace 字段构造共享 FormatFlag 位集合 / Build the shared `FormatFlag` bitset from one parsed brace field
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @param upper_case 最终展示是否使用大写字母 / Whether the final presentation should use uppercase letters
 * @return 返回该字段对应的共享 `FormatFlag` 位集合 / Returns the shared `FormatFlag` bitset for this field
 */
[[nodiscard]] constexpr uint8_t BuildFlags(const ParsedField& parsed, bool upper_case)
{
  uint8_t flags = 0;
  if (parsed.align == Align::Left)
  {
    flags |= static_cast<uint8_t>(FormatFlag::LeftAlign);
  }
  if (parsed.align == Align::Center)
  {
    flags |= static_cast<uint8_t>(FormatFlag::CenterAlign);
  }
  if (parsed.force_sign)
  {
    flags |= static_cast<uint8_t>(FormatFlag::ForceSign);
  }
  if (parsed.space_sign)
  {
    flags |= static_cast<uint8_t>(FormatFlag::SpaceSign);
  }
  if (parsed.alternate)
  {
    flags |= static_cast<uint8_t>(FormatFlag::Alternate);
  }
  if (parsed.zero_pad && parsed.align == Align::None)
  {
    flags |= static_cast<uint8_t>(FormatFlag::ZeroPad);
  }
  if (upper_case)
  {
    flags |= static_cast<uint8_t>(FormatFlag::UpperCase);
  }
  return flags;
}

/**
 * @brief 根据 brace 字段属性构造一条共享 `FormatField` 记录 / Build one shared `FormatField` record from parsed brace-field properties
 * @param parsed 已解析的 brace 字段 / Parsed brace field
 * @param type 共享运行期字段类型 / Shared runtime field type
 * @param pack 运行期参数打包类型 / Runtime packed-argument kind
 * @param upper_case 最终展示是否使用大写字母 / Whether the final presentation should use uppercase letters
 * @return 返回共享编译后端要消费的 `FormatField` 记录 / Returns the shared `FormatField` record consumed by the compile-time backend
 */
[[nodiscard]] constexpr FormatField MakeField(const ParsedField& parsed, FormatType type,
                                              FormatPackKind pack, bool upper_case = false)
{
  return FormatField{
      .type = type,
      .pack = pack,
      .rule = FormatArgumentRule::None,
      .flags = BuildFlags(parsed, upper_case),
      .fill = parsed.fill,
      .width = parsed.width,
      .precision = parsed.has_precision ? parsed.precision : UnspecifiedPrecision(),
  };
}

/**
 * @brief 判断展示字符是否缺省或等于目标字符 / Return whether the presentation is absent or matches the expected token
 * @param presentation 已解析展示字符；缺省时为 0 / Parsed presentation token, or zero when absent
 * @param expected 期望的展示字符 / Expected presentation token
 * @return 若展示字符缺省或等于 `expected` 则返回 `true` / Returns `true` when the presentation is absent or matches `expected`
 */
[[nodiscard]] constexpr bool IsDefaultOr(char presentation, char expected)
{
  return presentation == 0 || presentation == expected;
}

/**
 * @brief 判断展示字符是否选择了非十进制整数族 / Return whether the presentation selects one non-decimal integer family
 * @param presentation 已解析展示字符 / Parsed presentation token
 * @return 若选择了二进制、八进制或十六进制展示则返回 `true`，否则返回 `false` / Returns `true` for binary, octal, or hex presentations, otherwise `false`
 */
[[nodiscard]] constexpr bool IsNonDecimalPresentation(char presentation)
{
  return presentation == 'b' || presentation == 'B' || presentation == 'o' ||
         presentation == 'x' || presentation == 'X';
}

/**
 * @brief 在当前功能开关下选择前端默认浮点展示字符 / Choose the frontend default float presentation under current feature gates
 * @return 返回默认浮点展示字符；若全部浮点展示都被关闭则返回 0 / Returns the default float presentation token, or zero when all float presentations are disabled
 */
[[nodiscard]] constexpr char DefaultFloatPresentation()
{
  if (Config::enable_float_general)
  {
    return 'g';
  }
  if (Config::enable_float_fixed)
  {
    return 'f';
  }
  if (Config::enable_float_scientific)
  {
    return 'e';
  }
  return 0;
}
}  // namespace ArgumentResolution
