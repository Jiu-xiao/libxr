#pragma once

#include <array>
#include <limits>
#include <string_view>

#include "format_compile.hpp"

namespace LibXR::Print::Detail::PrintfCompile
{
using Error = Printf::Error;
using Length = Printf::Length;

/**
 * @brief Frontend-only semantic conversion families before runtime lowering.
 * @brief 降为运行时类型之前，仅供前端使用的语义转换族。
 *
 * This layer is intentionally narrower than FormatType. It only describes what
 * the source-level printf token means before target-dependent width selection,
 * float storage selection, and argument packing are decided.
 * 这一层刻意比 FormatType 更窄，只描述源级 printf 说明符本身的语义，不提前决定
 * 目标相关的整数宽度、浮点存储宽度以及参数打包方式。
 */
enum class ValueKind : uint8_t
{
  None,               ///< invalid or unresolved specifier / 无效或尚未解析的说明符
  Signed,             ///< %d / %i family / %d / %i 族
  Unsigned,           ///< %u family / %u 族
  Octal,              ///< %o family / %o 族
  HexLower,           ///< %x family / %x 族
  HexUpper,           ///< %X family / %X 族
  Pointer,            ///< %p / %p
  Character,          ///< %c / %c
  String,             ///< %s / %s
  FloatFixed,         ///< %f / %F / %f / %F
  FloatScientific,    ///< %e / %E / %e / %E
  FloatGeneral,       ///< %g / %G / %g / %G
};

/**
 * @brief One parsed printf conversion before lowering into the shared format.
 * @brief 单个 printf 转换在降为共享格式前的解析结果。
 */
struct Conversion
{
  ValueKind type = ValueKind::None;   ///< semantic conversion category / 转换项归一化后的语义类别
  Length length = Length::Default;    ///< parsed length modifier / 已解析的长度修饰符
  bool left_align = false;            ///< parsed - flag / 已解析的 - 标志
  bool force_sign = false;            ///< parsed + flag / 已解析的 + 标志
  bool space_sign = false;            ///< parsed space-sign flag / 已解析的空格正号标志
  bool alternate = false;             ///< parsed # flag / 已解析的 # 标志
  bool zero_pad = false;              ///< parsed 0 flag / 已解析的 0 标志
  bool upper_case = false;            ///< implied uppercase output / 隐含的大写输出标志
  uint8_t width = 0;                  ///< parsed field width / 已解析的字段宽度
  bool has_precision = false;         ///< whether precision was explicitly provided / 是否显式提供了精度
  uint8_t precision = 0;              ///< parsed precision value / 已解析的精度值

  /// Packs the parsed frontend flags into the shared FormatFlag byte. / 将前端解析出的标志位打包成共享的 FormatFlag 字节
  [[nodiscard]] constexpr uint8_t FlagsByte() const
  {
    uint8_t flags = 0;
    if (left_align)
    {
      flags |= static_cast<uint8_t>(FormatFlag::LeftAlign);
    }
    if (force_sign)
    {
      flags |= static_cast<uint8_t>(FormatFlag::ForceSign);
    }
    if (space_sign)
    {
      flags |= static_cast<uint8_t>(FormatFlag::SpaceSign);
    }
    if (alternate)
    {
      flags |= static_cast<uint8_t>(FormatFlag::Alternate);
    }
    if (zero_pad)
    {
      flags |= static_cast<uint8_t>(FormatFlag::ZeroPad);
    }
    if (upper_case)
    {
      flags |= static_cast<uint8_t>(FormatFlag::UpperCase);
    }
    return flags;
  }

  /// Returns the shared precision byte, or 0xFF when precision is absent. / 返回共享精度字节；若未显式指定精度则为 0xFF
  [[nodiscard]] constexpr uint8_t PrecisionByte() const
  {
    return has_precision ? precision : std::numeric_limits<uint8_t>::max();
  }
};

/**
 * @brief Feature-switch families referenced by printf conversion descriptors.
 * @brief printf 转换说明符描述表使用的功能开关类别。
 */
enum class FeatureGate : uint8_t
{
  Integer,          ///< %d / %i / %u family / %d / %i / %u 整数族
  IntegerBase8_16,  ///< %o / %x / %X / %o / %x / %X
  Pointer,          ///< %p / %p
  Text,             ///< %c / %s / %c / %s
  FloatFixed,       ///< %f / %F / %f / %F
  FloatScientific,  ///< %e / %E / %e / %E
  FloatGeneral,     ///< %g / %G / %g / %G
};

/**
 * @brief Length families accepted by one printf conversion descriptor.
 * @brief 单个 printf 转换说明符允许的长度修饰类别。
 */
enum class LengthPolicy : uint8_t
{
  Integer,   ///< integer lengths except L / 接受整数长度修饰，但不接受 L
  NoneOnly,  ///< only the default no-length form / 只接受默认无长度修饰
  Float,     ///< default or L / 只接受默认或 L
};

/**
 * @brief One source-level printf specifier descriptor.
 * @brief 单个源级 printf 说明符描述项。
 */
struct SpecifierDescriptor
{
  char token = 0;  ///< source conversion character / 源格式转换字符
  ValueKind type = ValueKind::None;   ///< normalized semantic category / 归一化后的语义类别
  FeatureGate gate =
      FeatureGate::Integer;  ///< feature gate controlling this specifier / 控制该说明符的功能开关
  LengthPolicy length_policy =
      LengthPolicy::NoneOnly;  ///< accepted length family / 允许的长度修饰类别
  bool upper_case = false;  ///< whether the specifier implies uppercase output / 说明符是否隐含大写输出
};

inline constexpr size_t length_rule_count =
    static_cast<size_t>(Length::LongDouble) + 1;

inline constexpr std::array<FormatArgumentRule, length_rule_count> signed_rules{
    FormatArgumentRule::SignedAny,
    FormatArgumentRule::SignedChar,
    FormatArgumentRule::SignedShort,
    FormatArgumentRule::SignedLong,
    FormatArgumentRule::SignedLongLong,
    FormatArgumentRule::SignedIntMax,
    FormatArgumentRule::SignedSize,
    FormatArgumentRule::SignedPtrDiff,
    FormatArgumentRule::None,
};

/// Per-length unsigned argument-rule table indexed by normalized printf length. / 按归一化 printf 长度索引的无符号参数规则表
inline constexpr std::array<FormatArgumentRule, length_rule_count> unsigned_rules{
    FormatArgumentRule::UnsignedAny,
    FormatArgumentRule::UnsignedChar,
    FormatArgumentRule::UnsignedShort,
    FormatArgumentRule::UnsignedLong,
    FormatArgumentRule::UnsignedLongLong,
    FormatArgumentRule::UnsignedIntMax,
    FormatArgumentRule::UnsignedSize,
    FormatArgumentRule::UnsignedPtrDiff,
    FormatArgumentRule::None,
};

/// Source-level printf specifier descriptor table. / 源级 printf 说明符描述表
inline constexpr std::array<SpecifierDescriptor, 15> specifiers{{
    {'d', ValueKind::Signed, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'i', ValueKind::Signed, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'u', ValueKind::Unsigned, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'o', ValueKind::Octal, FeatureGate::IntegerBase8_16, LengthPolicy::Integer, false},
    {'x', ValueKind::HexLower, FeatureGate::IntegerBase8_16, LengthPolicy::Integer,
     false},
    {'X', ValueKind::HexUpper, FeatureGate::IntegerBase8_16, LengthPolicy::Integer,
     false},
    {'p', ValueKind::Pointer, FeatureGate::Pointer, LengthPolicy::NoneOnly, false},
    {'c', ValueKind::Character, FeatureGate::Text, LengthPolicy::NoneOnly, false},
    {'s', ValueKind::String, FeatureGate::Text, LengthPolicy::NoneOnly, false},
    {'f', ValueKind::FloatFixed, FeatureGate::FloatFixed, LengthPolicy::Float, false},
    {'F', ValueKind::FloatFixed, FeatureGate::FloatFixed, LengthPolicy::Float, true},
    {'e', ValueKind::FloatScientific, FeatureGate::FloatScientific,
     LengthPolicy::Float, false},
    {'E', ValueKind::FloatScientific, FeatureGate::FloatScientific,
     LengthPolicy::Float, true},
    {'g', ValueKind::FloatGeneral, FeatureGate::FloatGeneral, LengthPolicy::Float,
     false},
    {'G', ValueKind::FloatGeneral, FeatureGate::FloatGeneral, LengthPolicy::Float,
     true},
}};

/// Returns whether one source byte is an ASCII decimal digit. / 判断一个源字节是否为 ASCII 十进制数字
[[nodiscard]] constexpr bool IsDigit(char ch)
{
  return ch >= '0' && ch <= '9';
}

/// Returns whether the source contains an embedded NUL byte before the terminator. / 判断源字符串在结尾终止字节之前是否包含嵌入式 NUL
[[nodiscard]] constexpr bool HasEmbeddedNul(std::string_view source)
{
  for (char ch : source)
  {
    if (ch == '\0')
    {
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr size_t LengthIndex(Length length)
{
  return static_cast<size_t>(length);
}

/// Returns whether one frontend semantic family should use unsigned storage/rules. / 判断某个前端语义族是否应使用无符号存储与匹配规则
[[nodiscard]] constexpr bool IsUnsignedType(ValueKind type)
{
  switch (type)
  {
    case ValueKind::Unsigned:
    case ValueKind::Octal:
    case ValueKind::HexLower:
    case ValueKind::HexUpper:
      return true;
    default:
      return false;
  }
}

/// Looks up one source conversion character in the descriptor table. / 在描述表中查找一个源级转换字符
[[nodiscard]] consteval SpecifierDescriptor LookupSpecifier(char token)
{
  for (const auto& descriptor : specifiers)
  {
    if (descriptor.token == token)
    {
      return descriptor;
    }
  }

  return {};
}

/// Returns whether one specifier family is enabled under the current feature gates. / 判断某个说明符家族在当前功能开关下是否启用
[[nodiscard]] constexpr bool TypeEnabled(FeatureGate gate, Length length)
{
  switch (gate)
  {
    case FeatureGate::Integer:
      return Config::enable_integer;
    case FeatureGate::IntegerBase8_16:
      return Config::enable_integer_base8_16;
    case FeatureGate::Pointer:
      return Config::enable_pointer;
    case FeatureGate::Text:
      return Config::enable_text;
    case FeatureGate::FloatFixed:
      return Config::enable_float_fixed &&
             (length != Length::LongDouble || Config::enable_float_long_double);
    case FeatureGate::FloatScientific:
      return Config::enable_float_scientific &&
             (length != Length::LongDouble || Config::enable_float_long_double);
    case FeatureGate::FloatGeneral:
      return Config::enable_float_general &&
             (length != Length::LongDouble || Config::enable_float_long_double);
  }

  return false;
}

/// Returns whether one parsed length modifier is legal for the selected specifier family. / 判断某个长度修饰符对当前说明符家族是否合法
[[nodiscard]] constexpr bool LengthAllowed(LengthPolicy policy, Length length)
{
  switch (policy)
  {
    case LengthPolicy::Integer:
      return length != Length::LongDouble;
    case LengthPolicy::NoneOnly:
      return length == Length::Default;
    case LengthPolicy::Float:
      return length == Length::Default || length == Length::LongDouble;
  }

  return false;
}

/// Returns the argument rule selected by one normalized length index. / 返回某个归一化长度索引选中的参数匹配规则
[[nodiscard]] constexpr FormatArgumentRule RuleFromTable(
    const std::array<FormatArgumentRule, length_rule_count>& rules, Length length)
{
  return rules[LengthIndex(length)];
}

/// Returns whether one argument rule requires 64-bit packed storage on this target. / 判断某个参数规则在当前目标上是否需要 64 位打包存储
[[nodiscard]] consteval bool RuleUses64BitStorage(FormatArgumentRule rule)
{
  switch (rule)
  {
    case FormatArgumentRule::SignedLong:
      return sizeof(long) > sizeof(int32_t);
    case FormatArgumentRule::SignedLongLong:
      return sizeof(long long) > sizeof(int32_t);
    case FormatArgumentRule::SignedIntMax:
      return sizeof(intmax_t) > sizeof(int32_t);
    case FormatArgumentRule::SignedSize:
      return sizeof(std::make_signed_t<size_t>) > sizeof(int32_t);
    case FormatArgumentRule::SignedPtrDiff:
      return sizeof(ptrdiff_t) > sizeof(int32_t);
    case FormatArgumentRule::UnsignedLong:
      return sizeof(unsigned long) > sizeof(uint32_t);
    case FormatArgumentRule::UnsignedLongLong:
      return sizeof(unsigned long long) > sizeof(uint32_t);
    case FormatArgumentRule::UnsignedIntMax:
      return sizeof(uintmax_t) > sizeof(uint32_t);
    case FormatArgumentRule::UnsignedSize:
      return sizeof(size_t) > sizeof(uint32_t);
    case FormatArgumentRule::UnsignedPtrDiff:
      return sizeof(std::make_unsigned_t<ptrdiff_t>) > sizeof(uint32_t);
    default:
      return false;
  }
}

/// Returns whether ordinary printf float conversions lower to double storage. / 判断普通 printf 浮点转换是否降为 double 存储
[[nodiscard]] constexpr bool UsesDoubleFloatStorage()
{
  return Config::enable_float_double;
}

/**
 * @brief Lowers one parsed printf conversion to its semantic runtime type.
 * @brief 将一个已解析的 printf 转换降为语义运行期类型。
 */
[[nodiscard]] consteval FormatType LowerType(const Conversion& conversion,
                                             FormatArgumentRule rule)
{
  if (conversion.length == Length::LongDouble)
  {
    if (conversion.type == ValueKind::FloatFixed)
    {
      return FormatType::LongDoubleFixed;
    }
    if (conversion.type == ValueKind::FloatScientific)
    {
      return FormatType::LongDoubleScientific;
    }
    if (conversion.type == ValueKind::FloatGeneral)
    {
      return FormatType::LongDoubleGeneral;
    }
  }

  if (conversion.type == ValueKind::Signed)
  {
    return RuleUses64BitStorage(rule) ? FormatType::Signed64 : FormatType::Signed32;
  }
  if (conversion.type == ValueKind::Unsigned)
  {
    return RuleUses64BitStorage(rule) ? FormatType::Unsigned64
                                      : FormatType::Unsigned32;
  }
  if (conversion.type == ValueKind::Octal)
  {
    return RuleUses64BitStorage(rule) ? FormatType::Octal64 : FormatType::Octal32;
  }
  if (conversion.type == ValueKind::HexLower)
  {
    return RuleUses64BitStorage(rule) ? FormatType::HexLower64
                                      : FormatType::HexLower32;
  }
  if (conversion.type == ValueKind::HexUpper)
  {
    return RuleUses64BitStorage(rule) ? FormatType::HexUpper64
                                      : FormatType::HexUpper32;
  }

  switch (conversion.type)
  {
    case ValueKind::Pointer:
      return FormatType::Pointer;
    case ValueKind::Character:
      return FormatType::Character;
    case ValueKind::String:
      return FormatType::String;
    case ValueKind::FloatFixed:
      return UsesDoubleFloatStorage() ? FormatType::DoubleFixed
                                      : FormatType::FloatFixed;
    case ValueKind::FloatScientific:
      return UsesDoubleFloatStorage() ? FormatType::DoubleScientific
                                      : FormatType::FloatScientific;
    case ValueKind::FloatGeneral:
      return UsesDoubleFloatStorage() ? FormatType::DoubleGeneral
                                      : FormatType::FloatGeneral;
    case ValueKind::None:
    case ValueKind::Signed:
    case ValueKind::Unsigned:
    case ValueKind::Octal:
    case ValueKind::HexLower:
    case ValueKind::HexUpper:
      return FormatType::End;
  }

  return FormatType::End;
}

/// Maps one semantic runtime type to its packed runtime storage kind. / 将一个语义运行期类型映射到其打包存储类别
[[nodiscard]] consteval FormatPackKind LowerPackKind(FormatType type)
{
  switch (type)
  {
    case FormatType::Signed32:
      return FormatPackKind::I32;
    case FormatType::Signed64:
      return FormatPackKind::I64;
    case FormatType::Unsigned32:
    case FormatType::Octal32:
    case FormatType::HexLower32:
    case FormatType::HexUpper32:
      return FormatPackKind::U32;
    case FormatType::Unsigned64:
    case FormatType::Octal64:
    case FormatType::HexLower64:
    case FormatType::HexUpper64:
      return FormatPackKind::U64;
    case FormatType::Pointer:
      return FormatPackKind::Pointer;
    case FormatType::Character:
      return FormatPackKind::Character;
    case FormatType::String:
      return FormatPackKind::StringView;
    case FormatType::FloatFixed:
    case FormatType::FloatScientific:
    case FormatType::FloatGeneral:
      return FormatPackKind::F32;
    case FormatType::DoubleFixed:
    case FormatType::DoubleScientific:
    case FormatType::DoubleGeneral:
      return FormatPackKind::F64;
    case FormatType::LongDoubleFixed:
    case FormatType::LongDoubleScientific:
    case FormatType::LongDoubleGeneral:
      return FormatPackKind::LongDouble;
    case FormatType::End:
    case FormatType::TextInline:
    case FormatType::TextRef:
    case FormatType::TextSpace:
      return FormatPackKind::U32;
  }

  return FormatPackKind::U32;
}

/// Lowers one parsed conversion to the compile-time argument matching rule it consumes. / 将一个已解析转换降为它消耗的编译期参数匹配规则
[[nodiscard]] consteval FormatArgumentRule LowerRule(const Conversion& conversion)
{
  if (conversion.type == ValueKind::Signed)
  {
    return RuleFromTable(signed_rules, conversion.length);
  }

  if (IsUnsignedType(conversion.type))
  {
    return RuleFromTable(unsigned_rules, conversion.length);
  }

  switch (conversion.type)
  {
    case ValueKind::Pointer:
      return FormatArgumentRule::Pointer;
    case ValueKind::Character:
      return FormatArgumentRule::Character;
    case ValueKind::String:
      return FormatArgumentRule::String;
    case ValueKind::FloatFixed:
    case ValueKind::FloatScientific:
    case ValueKind::FloatGeneral:
      return (conversion.length == Length::LongDouble)
                 ? FormatArgumentRule::LongDouble
                 : FormatArgumentRule::Float;
    case ValueKind::None:
      break;
  }

  return FormatArgumentRule::None;
}

/// Validates target-dependent lowering constraints after parsing. / 在解析后校验与目标相关的降级约束
[[nodiscard]] consteval Error ValidateConversion(const Conversion& conversion)
{
  if ((conversion.type == ValueKind::Signed || IsUnsignedType(conversion.type)) &&
      RuleUses64BitStorage(LowerRule(conversion)) && !Config::enable_integer_64bit)
  {
    return Error::InvalidLength;
  }

  return Error::None;
}

/// Lowers one parsed printf conversion into the shared FormatField triple of type, pack, and rule. / 将一个已解析 printf 转换降为共享 FormatField 的 type、pack、rule 三元组
[[nodiscard]] consteval FormatField LowerField(const Conversion& conversion)
{
  auto rule = LowerRule(conversion);
  auto type = LowerType(conversion, rule);
  return FormatField{
      .type = type,
      .pack = LowerPackKind(type),
      .rule = rule,
      .flags = conversion.FlagsByte(),
      .width = conversion.width,
      .precision = conversion.PrecisionByte(),
  };
}

/**
 * @brief Parses one decimal uint8_t field such as width or precision.
 * @brief 解析单个十进制 uint8_t 字段，例如宽度或精度。
 */
[[nodiscard]] consteval Error ParseByte(std::string_view source, size_t& pos,
                                        uint8_t limit, uint8_t& value)
{
  value = 0;
  if (pos >= source.size() || !IsDigit(source[pos]))
  {
    return Error::None;
  }

  while (pos < source.size() && IsDigit(source[pos]))
  {
    auto digit = static_cast<uint8_t>(source[pos] - '0');
    if (value > static_cast<uint8_t>((limit - digit) / 10))
    {
      return Error::NumberOverflow;
    }

    value = static_cast<uint8_t>(value * 10 + digit);
    ++pos;
  }

  return Error::None;
}

/// Parses the leading flag cluster of one conversion. / 解析单个转换项前导的标志位簇
consteval void ParseFlags(std::string_view source, size_t& pos, Conversion& conversion)
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
        conversion.alternate = true;
        break;
      case '0':
        conversion.zero_pad = true;
        break;
      default:
        return;
    }
    ++pos;
  }
}

/// Parses one optional constant width field. / 解析一个可选的常量宽度字段
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

  return ParseByte(source, pos, std::numeric_limits<uint8_t>::max(), conversion.width);
}

/// Parses one optional constant precision field. / 解析一个可选的常量精度字段
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

consteval void ParseLength(std::string_view source, size_t& pos, Conversion& conversion)
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

[[nodiscard]] consteval Error ParseSpecifier(std::string_view source, size_t& pos,
                                             Conversion& conversion)
{
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  auto descriptor = LookupSpecifier(source[pos]);
  if (descriptor.type == ValueKind::None)
  {
    return Error::InvalidSpecifier;
  }
  if (!LengthAllowed(descriptor.length_policy, conversion.length))
  {
    return Error::InvalidLength;
  }
  if (!TypeEnabled(descriptor.gate, conversion.length))
  {
    return Error::InvalidSpecifier;
  }

  conversion.type = descriptor.type;
  conversion.upper_case = conversion.upper_case || descriptor.upper_case;
  ++pos;
  return Error::None;
}

[[nodiscard]] consteval Error Parse(std::string_view source, size_t& pos,
                                    Conversion& conversion)
{
  ++pos;
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  ParseFlags(source, pos, conversion);

  auto error = ParseWidth(source, pos, conversion);
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

  return ValidateConversion(conversion);
}

[[nodiscard]] consteval Error Walk(std::string_view source, auto& visitor)
{
  if (HasEmbeddedNul(source))
  {
    return Error::EmbeddedNul;
  }

  size_t pos = 0;
  size_t text_begin = 0;

  while (pos < source.size())
  {
    if (source[pos] != '%')
    {
      ++pos;
      continue;
    }

    auto error = visitor.Text(text_begin, pos - text_begin);
    if (error != Error::None)
    {
      return error;
    }

    if (pos + 1 < source.size() && source[pos + 1] == '%')
    {
      error = visitor.Text(pos, 1);
      if (error != Error::None)
      {
        return error;
      }

      pos += 2;
      text_begin = pos;
      continue;
    }

    auto parse_pos = pos;
    Conversion conversion{};
    error = Parse(source, parse_pos, conversion);
    if (error != Error::None)
    {
      return error;
    }

    error = visitor.Field(LowerField(conversion));
    if (error != Error::None)
    {
      return error;
    }

    pos = parse_pos;
    text_begin = pos;
  }

  return visitor.Text(text_begin, source.size() - text_begin);
}
}  // namespace LibXR::Print::Detail::PrintfCompile

namespace LibXR::Print
{
/**
 * @brief Compile-time printf frontend that parses and lowers one source string.
 * @brief 单个 printf 源字符串的编译期前端，负责解析并降为共享格式语义。
 */
template <Text Source>
class Printf::Compiler
{
 public:
  using ErrorType = Printf::Error;

  /// Returns the underlying source bytes without the terminating zero byte. / 返回去掉结尾零字节后的源字符串数据
  [[nodiscard]] static constexpr const char* SourceData() { return Source.Data(); }
  /// Returns the source length without the terminating zero byte. / 返回去掉结尾零字节后的源字符串长度
  [[nodiscard]] static constexpr size_t SourceSize() { return Source.Size(); }

  /// Walks normalized text spans and value fields in source order. / 按源串顺序遍历归一化后的文本片段和字段
  [[nodiscard]] static consteval ErrorType Walk(auto& visitor)
  {
    return Detail::PrintfCompile::Walk(
        std::string_view(SourceData(), SourceSize()), visitor);
  }
};
}  // namespace LibXR::Print
