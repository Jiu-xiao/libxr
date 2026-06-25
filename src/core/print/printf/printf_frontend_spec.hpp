#pragma once

/**
 * @brief 降为运行时类型之前，仅供前端使用的语义转换族 / Frontend-only semantic conversion families before runtime lowering
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
  Binary,             ///< %b / %B family / %b / %B 族
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
 * @brief 单个 printf 转换在降为共享格式前的解析结果 / One parsed printf conversion before lowering into the shared format
 */
struct Conversion
{
  size_t arg_index = 0;            ///< source argument index consumed by this field / 当前字段消耗的源参数索引
  ValueKind type = ValueKind::None;   ///< semantic conversion category / 转换项归一化后的语义类别
  Length length = Length::Default;    ///< parsed length modifier / 已解析的长度修饰符
  bool left_align = false;            ///< parsed - flag / 已解析的 - 标志
  bool force_sign = false;            ///< parsed + flag / 已解析的 + 标志
  bool space_sign = false;            ///< parsed space-sign flag / 已解析的空格正号标志
  bool alternate = false;             ///< parsed # flag / 已解析的 # 标志
  bool zero_pad = false;              ///< parsed 0 flag / 已解析的 0 标志
  bool upper_case = false;            ///< implied uppercase output / 隐含的大写输出标志
  bool positional = false;            ///< whether arg_index came from n$ syntax / arg_index 是否来自 n$ 语法
  uint8_t width = 0;                  ///< parsed field width / 已解析的字段宽度
  bool has_precision = false;         ///< whether precision was explicitly provided / 是否显式提供了精度
  uint8_t precision = 0;              ///< parsed precision value / 已解析的精度值

  /**
   * @brief 将前端解析出的标志位打包成共享的 `FormatFlag` 字节 / Pack the parsed frontend flags into the shared `FormatFlag` byte
   * @return 返回打包后的共享 `FormatFlag` 字节 / Returns the packed shared `FormatFlag` byte
   */
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

  /**
   * @brief 返回共享精度字节；若未显式指定精度则为 `0xFF` / Return the shared precision byte, or `0xFF` when precision is absent
   * @return 返回运行期协议期望的共享精度字节 / Returns the shared precision byte expected by the runtime protocol
   */
  [[nodiscard]] constexpr uint8_t PrecisionByte() const
  {
    return has_precision ? precision : std::numeric_limits<uint8_t>::max();
  }
};

/**
 * @brief printf 转换说明符描述表使用的功能开关类别 / Feature-switch families referenced by printf conversion descriptors
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
 * @brief 单个 printf 转换说明符允许的长度修饰类别 / Length families accepted by one printf conversion descriptor
 */
enum class LengthPolicy : uint8_t
{
  Integer,   ///< integer lengths except L / 接受整数长度修饰，但不接受 L
  NoneOnly,  ///< only the default no-length form / 只接受默认无长度修饰
  Float,     ///< default or L / 只接受默认或 L
};

/**
 * @brief 单个源级 printf 说明符描述项 / One source-level printf specifier descriptor
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

/**
 * @brief 归一化 printf 长度表的宽度 / Normalized printf length-table width
 */
inline constexpr size_t length_rule_count =
    static_cast<size_t>(Length::LongDouble) + 1;

/**
 * @brief 按长度索引的有符号参数规则表 / Per-length signed argument-rule table
 */
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

/**
 * @brief 按归一化 printf 长度索引的无符号参数规则表 / Per-length unsigned argument-rule table indexed by normalized printf length
 */
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

/**
 * @brief 源级 printf 说明符描述表 / Source-level printf specifier descriptor table
 */
inline constexpr std::array<SpecifierDescriptor, 17> specifiers{{
    {'d', ValueKind::Signed, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'i', ValueKind::Signed, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'u', ValueKind::Unsigned, FeatureGate::Integer, LengthPolicy::Integer, false},
    {'b', ValueKind::Binary, FeatureGate::IntegerBase8_16, LengthPolicy::Integer, false},
    {'B', ValueKind::Binary, FeatureGate::IntegerBase8_16, LengthPolicy::Integer, true},
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
