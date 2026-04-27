#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "format_compile.hpp"
#include "format_argument.hpp"
#include "printf.hpp"

namespace LibXR::Print::Detail::FormatFrontend
{
/**
 * @brief Compile-time failure categories for the brace-style format frontend.
 * @brief brace 风格 format 前端的编译期失败类别。
 */
enum class Error : uint8_t
{
  None,                    ///< success / 成功
  NumberOverflow,          ///< index / width / precision does not fit its target field / 索引、宽度或精度超出目标字段容量
  UnexpectedEnd,           ///< source ended in the middle of one field / 字段尚未结束时源串已结束
  EmbeddedNul,             ///< embedded NUL before the terminator / 终止字节之前出现嵌入式 NUL
  UnmatchedBrace,          ///< unmatched { or } / 花括号未正确配对
  MixedIndexing,           ///< automatic and manual indexing were mixed / 混用了自动索引与手动索引
  DynamicField,            ///< nested replacement field in width / precision is unsupported / 不支持宽度或精度中的嵌套替换字段
  InvalidArgumentIndex,    ///< field head is not a valid decimal argument index / 字段开头不是合法的十进制参数索引
  InvalidSpecifier,        ///< malformed format-spec grammar / format-spec 语法非法
  InvalidPresentation,     ///< unsupported presentation type character / 不支持的展示类型字符
  MissingArgument,         ///< referenced argument index is out of range / 引用的参数索引超出实参数量
  ArgumentTypeMismatch,    ///< format options are incompatible with the selected argument type / 格式选项与选中的参数类型不兼容
  UnsupportedArgumentType,  ///< C++ argument type is not supported by this frontend / 当前前端不支持该 C++ 参数类型
  TextOffsetOverflow,      ///< referenced text offset no longer fits in uint16_t / 文本池偏移超出 uint16_t
  TextSizeOverflow,        ///< referenced text size no longer fits in uint16_t / 文本长度超出 uint16_t
};

/**
 * @brief Parsed alignment directive before lowering into FormatFlag bits.
 * @brief 降为 FormatFlag 位之前的已解析对齐方式。
 */
enum class Align : uint8_t
{
  None,    ///< default alignment / 默认对齐
  Left,    ///< '<' / 左对齐
  Right,   ///< '>' / 右对齐
  Center,  ///< '^' / 居中对齐
};

/**
 * @brief Parsed brace field before binding it to one concrete C++ argument type.
 * @brief 在绑定到具体 C++ 参数类型之前的 brace 字段解析结果。
 */
struct ParsedField
{
  size_t arg_index = 0;     ///< selected source argument index / 选中的源参数索引
  Align align = Align::None;  ///< parsed alignment / 已解析对齐方式
  char fill = ' ';          ///< parsed fill character / 已解析填充字符
  bool force_sign = false;  ///< parsed plus-sign option / 已解析正号选项
  bool space_sign = false;  ///< parsed space-sign option / 已解析空格符号选项
  bool alternate = false;   ///< parsed alternate-form option / 已解析备用格式选项
  bool zero_pad = false;    ///< parsed zero-pad option / 已解析零填充选项
  uint8_t width = 0;        ///< parsed constant width / 已解析常量宽度
  bool has_precision = false;  ///< whether precision was explicitly present / 是否显式给出了精度
  uint8_t precision = 0;    ///< parsed precision when present / 显式给出时的精度值
  char presentation = 0;    ///< parsed presentation character, or 0 for default / 展示类型字符；缺省时为 0
};

/**
 * @brief Normalized C++ argument families seen by the brace frontend.
 * @brief brace 前端看到的规范化 C++ 参数族。
 */
enum class BoundKind : uint8_t
{
  Unsupported,  ///< unsupported C++ argument type / 不支持的 C++ 参数类型
  Bool,         ///< bool / bool
  Character,    ///< exact char / 精确 char
  Signed,       ///< signed integer / 有符号整数
  Unsigned,     ///< unsigned integer / 无符号整数
  String,       ///< string-like / 字符串类
  Pointer,      ///< pointer-like / 指针类
  Float32,      ///< float / float
  Float64,      ///< double / double
  LongDouble,   ///< long double / long double
};

/**
 * @brief One normalized C++ argument together with its storage-width decision.
 * @brief 单个规范化 C++ 参数及其存储宽度决策。
 */
struct BoundArgument
{
  BoundKind kind = BoundKind::Unsupported;  ///< normalized argument category / 归一化后的参数类别
  bool uses_64bit_storage = false;          ///< whether integer storage must be 64-bit / 整数是否必须走 64 位存储
};

/**
 * @brief Result of lowering one parsed brace field into the shared format protocol.
 * @brief 将单个已解析 brace 字段降为共享格式协议后的结果。
 */
struct LoweredField
{
  Error error = Error::None;  ///< lowering result / lowering 结果
  FormatField field{};        ///< lowered shared field / 降为共享协议后的字段
};

namespace SourceSyntax
{
/**
 * @brief Source-only analysis data for one brace-style format literal.
 * @brief 单条 brace 风格格式字面量的仅源串分析数据。
 */
template <size_t FieldCount>
struct SourceAnalysis
{
  std::array<size_t, FieldCount> argument_order{};  ///< source-ordered argument references / 按源串顺序引用的参数索引
  size_t required_argument_count = 0;  ///< minimum call-site argument count / 调用点至少需要的参数个数
  Error error = Error::None;           ///< first source-only parse error / 首个仅源串解析错误
};

/**
 * @brief Conservative temporary accumulator used during source-only analysis.
 * @brief 仅源串分析阶段使用的保守临时累加器。
 */
template <size_t MaxFieldCount>
struct SourceAnalysisScratch
{
  std::array<size_t, MaxFieldCount> order{};  ///< conservative field-order scratch buffer / 按字段顺序记录参数索引的临时缓冲区
  size_t field_count = 0;                     ///< parsed replacement-field count / 已解析的替换字段数量
  size_t required_argument_count = 0;         ///< minimum call-site argument count / 调用点至少需要的参数个数
  Error error = Error::None;                  ///< first parse error / 首个解析错误

  [[nodiscard]] consteval Error Text(size_t, size_t) const { return Error::None; }

  [[nodiscard]] consteval Error Field(const ParsedField& field)
  {
    order[field_count++] = field.arg_index;
    size_t used_argument_count = field.arg_index + 1;
    if (used_argument_count > required_argument_count)
    {
      required_argument_count = used_argument_count;
    }
    return Error::None;
  }
};

/**
 * @brief Source parser helpers for the brace-style grammar.
 * @brief brace 风格语法的源串解析辅助函数。
 */
[[nodiscard]] constexpr bool IsDigit(char ch)
{
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool IsAlign(char ch)
{
  return ch == '<' || ch == '>' || ch == '^';
}

[[nodiscard]] constexpr Align ParseAlign(char ch)
{
  switch (ch)
  {
    case '<':
      return Align::Left;
    case '>':
      return Align::Right;
    case '^':
      return Align::Center;
    default:
      return Align::None;
  }
}

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

[[nodiscard]] constexpr bool IsSupportedPresentation(char ch)
{
  switch (ch)
  {
    case 'd':
    case 'b':
    case 'B':
    case 'o':
    case 'x':
    case 'X':
    case 'c':
    case 's':
    case 'p':
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
      return true;
    default:
      return false;
  }
}

template <typename UInt>
[[nodiscard]] consteval Error ParseUnsigned(std::string_view source, size_t& pos,
                                            UInt limit, UInt& value)
{
  static_assert(std::is_unsigned_v<UInt>);

  value = 0;
  if (pos >= source.size() || !IsDigit(source[pos]))
  {
    return Error::InvalidSpecifier;
  }

  while (pos < source.size() && IsDigit(source[pos]))
  {
    auto digit = static_cast<UInt>(source[pos] - '0');
    if (value > static_cast<UInt>((limit - digit) / 10))
    {
      return Error::NumberOverflow;
    }
    value = static_cast<UInt>(value * 10 + digit);
    ++pos;
  }

  return Error::None;
}

/**
 * @brief Source-level indexing mode for automatic versus manual brace fields.
 * @brief brace 字段自动索引与手动索引的源级状态。
 */
struct IndexingState
{
  bool uses_manual = false;  ///< manual field numbering is in use / 正在使用手动编号
  bool uses_auto = false;    ///< automatic field numbering is in use / 正在使用自动编号
  size_t next_auto_index = 0;  ///< next automatic argument index / 下一个自动参数索引
};

[[nodiscard]] consteval Error ParseFieldHead(std::string_view source, size_t& pos,
                                             IndexingState& indexing,
                                             ParsedField& field)
{
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  if (source[pos] == ':' || source[pos] == '}')
  {
    if (indexing.uses_manual)
    {
      return Error::MixedIndexing;
    }

    indexing.uses_auto = true;
    field.arg_index = indexing.next_auto_index++;
    return Error::None;
  }

  if (!IsDigit(source[pos]))
  {
    return Error::InvalidArgumentIndex;
  }

  size_t index = 0;
  auto error =
      ParseUnsigned(source, pos, std::numeric_limits<size_t>::max(), index);
  if (error != Error::None)
  {
    return error;
  }

  if (indexing.uses_auto)
  {
    return Error::MixedIndexing;
  }

  indexing.uses_manual = true;
  field.arg_index = index;
  return Error::None;
}

[[nodiscard]] consteval Error ParseFormatSpec(std::string_view source, size_t& pos,
                                              ParsedField& field)
{
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  if (pos + 1 < source.size() && IsAlign(source[pos + 1]))
  {
    if (source[pos] == '{' || source[pos] == '}')
    {
      return Error::InvalidSpecifier;
    }
    field.fill = source[pos];
    field.align = ParseAlign(source[pos + 1]);
    pos += 2;
  }
  else if (IsAlign(source[pos]))
  {
    field.align = ParseAlign(source[pos]);
    ++pos;
  }

  if (pos < source.size() &&
      (source[pos] == '+' || source[pos] == '-' || source[pos] == ' '))
  {
    field.force_sign = source[pos] == '+';
    field.space_sign = source[pos] == ' ';
    ++pos;
  }

  if (pos < source.size() && source[pos] == '#')
  {
    field.alternate = true;
    ++pos;
  }

  if (pos < source.size() && source[pos] == '0')
  {
    field.zero_pad = true;
    ++pos;
  }

  if (pos < source.size() && source[pos] == '{')
  {
    return Error::DynamicField;
  }

  if (pos < source.size() && IsDigit(source[pos]))
  {
    if (!Config::enable_width)
    {
      return Error::InvalidSpecifier;
    }
    auto error = ParseUnsigned(source, pos, std::numeric_limits<uint8_t>::max(),
                               field.width);
    if (error != Error::None)
    {
      return error;
    }
  }

  if (pos < source.size() && source[pos] == '.')
  {
    if (!Config::enable_precision)
    {
      return Error::InvalidSpecifier;
    }

    ++pos;
    if (pos < source.size() && source[pos] == '{')
    {
      return Error::DynamicField;
    }

    field.has_precision = true;
    auto error = ParseUnsigned(
        source, pos, static_cast<uint8_t>(std::numeric_limits<uint8_t>::max() - 1),
        field.precision);
    if (error != Error::None)
    {
      return error;
    }
  }

  if (pos < source.size() && source[pos] != '}')
  {
    field.presentation = source[pos++];
    if (!IsSupportedPresentation(field.presentation))
    {
      return Error::InvalidPresentation;
    }
  }

  return Error::None;
}

[[nodiscard]] consteval Error ParseField(std::string_view source, size_t& pos,
                                         IndexingState& indexing,
                                         ParsedField& field)
{
  ++pos;
  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }

  auto error = ParseFieldHead(source, pos, indexing, field);
  if (error != Error::None)
  {
    return error;
  }

  if (pos < source.size() && source[pos] != ':' && source[pos] != '}')
  {
    return Error::InvalidArgumentIndex;
  }

  if (pos < source.size() && source[pos] == ':')
  {
    ++pos;
    error = ParseFormatSpec(source, pos, field);
    if (error != Error::None)
    {
      return error;
    }
  }

  if (pos >= source.size())
  {
    return Error::UnexpectedEnd;
  }
  if (source[pos] != '}')
  {
    return Error::InvalidSpecifier;
  }

  ++pos;
  return Error::None;
}

/**
 * @brief Walks one brace-style source string and emits text spans plus parsed fields.
 * @brief 遍历一条 brace 风格源字符串，并发射文本片段与已解析字段。
 */
[[nodiscard]] consteval Error WalkSource(std::string_view source, auto& visitor)
{
  if (HasEmbeddedNul(source))
  {
    return Error::EmbeddedNul;
  }

  size_t pos = 0;
  size_t text_begin = 0;
  IndexingState indexing{};

  while (pos < source.size())
  {
    if (source[pos] == '{')
    {
      if (pos + 1 < source.size() && source[pos + 1] == '{')
      {
        auto error = visitor.Text(text_begin, pos - text_begin);
        if (error != Error::None)
        {
          return error;
        }
        error = visitor.Text(pos, 1);
        if (error != Error::None)
        {
          return error;
        }
        pos += 2;
        text_begin = pos;
        continue;
      }

      auto error = visitor.Text(text_begin, pos - text_begin);
      if (error != Error::None)
      {
        return error;
      }

      ParsedField field{};
      error = ParseField(source, pos, indexing, field);
      if (error != Error::None)
      {
        return error;
      }

      error = visitor.Field(field);
      if (error != Error::None)
      {
        return error;
      }

      text_begin = pos;
      continue;
    }

    if (source[pos] == '}')
    {
      if (pos + 1 < source.size() && source[pos + 1] == '}')
      {
        auto error = visitor.Text(text_begin, pos - text_begin);
        if (error != Error::None)
        {
          return error;
        }
        error = visitor.Text(pos, 1);
        if (error != Error::None)
        {
          return error;
        }
        pos += 2;
        text_begin = pos;
        continue;
      }

      return Error::UnmatchedBrace;
    }

    ++pos;
  }

  return visitor.Text(text_begin, source.size() - text_begin);
}

template <Text Source>
[[nodiscard]] consteval auto Analyze()
{
  constexpr auto scratch = []() consteval {
    SourceAnalysisScratch<Source.Size()> visitor{};
    visitor.error = WalkSource(std::string_view(Source.Data(), Source.Size()), visitor);
    return visitor;
  }();

  SourceAnalysis<scratch.field_count> result{};
  result.required_argument_count = scratch.required_argument_count;
  result.error = scratch.error;
  for (size_t i = 0; i < scratch.field_count; ++i)
  {
    result.argument_order[i] = scratch.order[i];
  }
  return result;
}
}  // namespace SourceSyntax

/**
 * @brief C++ argument normalization and lowering helpers.
 * @brief C++ 参数归一化与降级辅助函数。
 */
namespace ArgumentBinding
{

template <typename Arg>
[[nodiscard]] consteval BoundArgument DescribeArg()
{
  using Traits = FormatArgument::TypeTraits<Arg>;
  using Decayed = typename Traits::Decayed;
  using Normalized = typename Traits::Normalized;

  if constexpr (std::is_same_v<Decayed, bool>)
  {
    return BoundArgument{.kind = BoundKind::Bool};
  }
  else if constexpr (std::is_same_v<Decayed, char>)
  {
    return BoundArgument{.kind = BoundKind::Character};
  }
  else if constexpr (Traits::is_string_like)
  {
    return BoundArgument{.kind = BoundKind::String};
  }
  else if constexpr (Traits::is_pointer_like)
  {
    return BoundArgument{.kind = BoundKind::Pointer};
  }
  else if constexpr (Traits::is_signed_integer)
  {
    return BoundArgument{
        .kind = BoundKind::Signed,
        .uses_64bit_storage = sizeof(Normalized) > sizeof(int32_t),
    };
  }
  else if constexpr (Traits::is_unsigned_integer)
  {
    return BoundArgument{
        .kind = BoundKind::Unsigned,
        .uses_64bit_storage = sizeof(Normalized) > sizeof(uint32_t),
    };
  }
  else if constexpr (std::is_same_v<Decayed, float>)
  {
    return BoundArgument{.kind = BoundKind::Float32};
  }
  else if constexpr (std::is_same_v<Decayed, double>)
  {
    return BoundArgument{.kind = BoundKind::Float64};
  }
  else if constexpr (std::is_same_v<Decayed, long double>)
  {
    return BoundArgument{.kind = BoundKind::LongDouble};
  }
  else
  {
    return BoundArgument{};
  }
}

template <typename... Args>
[[nodiscard]] consteval auto DescribeArgs()
{
  return std::array<BoundArgument, sizeof...(Args)>{DescribeArg<Args>()...};
}

[[nodiscard]] constexpr bool HasSignOption(const ParsedField& field)
{
  return field.force_sign || field.space_sign;
}

[[nodiscard]] constexpr uint8_t UnspecifiedPrecision()
{
  return std::numeric_limits<uint8_t>::max();
}

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

[[nodiscard]] constexpr bool IsDefaultOr(char presentation, char expected)
{
  return presentation == 0 || presentation == expected;
}

[[nodiscard]] constexpr bool IsNonDecimalPresentation(char presentation)
{
  return presentation == 'b' || presentation == 'B' || presentation == 'o' ||
         presentation == 'x' || presentation == 'X';
}

[[nodiscard]] consteval LoweredField LowerIntegerLike(const ParsedField& parsed,
                                                      bool signed_decimal,
                                                      bool uses_64bit_storage)
{
  if (!Config::enable_integer)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (uses_64bit_storage && !Config::enable_integer_64bit)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  if (parsed.has_precision)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  if (parsed.presentation == 'c')
  {
    if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad)
    {
      return LoweredField{.error = Error::ArgumentTypeMismatch};
    }
    return LoweredField{.field = MakeField(parsed, FormatType::Character,
                                           FormatPackKind::Character)};
  }

  if (IsDefaultOr(parsed.presentation, 'd'))
  {
    if (parsed.alternate)
    {
      return LoweredField{.error = Error::ArgumentTypeMismatch};
    }
    if (!signed_decimal && HasSignOption(parsed))
    {
      return LoweredField{.error = Error::ArgumentTypeMismatch};
    }

    if (signed_decimal)
    {
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Signed64
                                                 : FormatType::Signed32,
                              uses_64bit_storage ? FormatPackKind::I64
                                                 : FormatPackKind::I32)};
    }

    return LoweredField{.field = MakeField(
                            parsed,
                            uses_64bit_storage ? FormatType::Unsigned64
                                               : FormatType::Unsigned32,
                            uses_64bit_storage ? FormatPackKind::U64
                                               : FormatPackKind::U32)};
  }

  if (!Config::enable_integer_base8_16 || HasSignOption(parsed))
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  switch (parsed.presentation)
  {
    case 'b':
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Binary64
                                                 : FormatType::Binary32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'B':
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Binary64
                                                 : FormatType::Binary32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32,
                              true)};
    case 'o':
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::Octal64
                                                 : FormatType::Octal32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'x':
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::HexLower64
                                                 : FormatType::HexLower32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32)};
    case 'X':
      return LoweredField{.field = MakeField(
                              parsed,
                              uses_64bit_storage ? FormatType::HexUpper64
                                                 : FormatType::HexUpper32,
                              uses_64bit_storage ? FormatPackKind::U64
                                                 : FormatPackKind::U32,
                              true)};
    default:
      return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
}

[[nodiscard]] consteval LoweredField LowerBool(const ParsedField& parsed)
{
  if (parsed.presentation == 0)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  return LowerIntegerLike(parsed, false, false);
}

[[nodiscard]] consteval LoweredField LowerCharacter(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 'c')
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad || parsed.has_precision)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_text)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  ParsedField adjusted = parsed;
  if (adjusted.align == Align::None)
  {
    adjusted.align = Align::Left;
  }

  return LoweredField{
      .field = MakeField(adjusted, FormatType::Character, FormatPackKind::Character)};
}

[[nodiscard]] consteval LoweredField LowerString(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 's')
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_text)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  ParsedField adjusted = parsed;
  if (adjusted.align == Align::None)
  {
    adjusted.align = Align::Left;
  }

  return LoweredField{
      .field = MakeField(adjusted, FormatType::String, FormatPackKind::StringView)};
}

[[nodiscard]] consteval LoweredField LowerPointer(const ParsedField& parsed)
{
  if (parsed.presentation != 0 && parsed.presentation != 'p')
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (parsed.alternate || HasSignOption(parsed) || parsed.zero_pad || parsed.has_precision)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }
  if (!Config::enable_pointer)
  {
    return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  return LoweredField{
      .field = MakeField(parsed, FormatType::Pointer, FormatPackKind::Pointer)};
}

[[nodiscard]] consteval LoweredField LowerFloat(const ParsedField& parsed,
                                                BoundKind kind)
{
  char presentation = parsed.presentation == 0 ? 'g' : parsed.presentation;
  bool upper_case =
      presentation == 'F' || presentation == 'E' || presentation == 'G';

  FormatType type = FormatType::End;
  FormatPackKind pack = FormatPackKind::F32;

  auto pick_type = [&](FormatType f32_type, FormatType f64_type,
                       FormatType ld_type) consteval -> bool {
    switch (kind)
    {
      case BoundKind::Float32:
        type = f32_type;
        pack = FormatPackKind::F32;
        return true;
      case BoundKind::Float64:
        type = Config::enable_float_double ? f64_type : f32_type;
        pack = Config::enable_float_double ? FormatPackKind::F64 : FormatPackKind::F32;
        return true;
      case BoundKind::LongDouble:
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
        return LoweredField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    case 'e':
    case 'E':
      if (!Config::enable_float_scientific ||
          !pick_type(FormatType::FloatScientific, FormatType::DoubleScientific,
                     FormatType::LongDoubleScientific))
      {
        return LoweredField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    case 'g':
    case 'G':
      if (!Config::enable_float_general ||
          !pick_type(FormatType::FloatGeneral, FormatType::DoubleGeneral,
                     FormatType::LongDoubleGeneral))
      {
        return LoweredField{.error = Error::ArgumentTypeMismatch};
      }
      break;
    default:
      return LoweredField{.error = Error::ArgumentTypeMismatch};
  }

  return LoweredField{.field = MakeField(parsed, type, pack, upper_case)};
}

template <typename... Args>
[[nodiscard]] consteval LoweredField LowerField(const ParsedField& parsed)
{
  constexpr auto bound_arguments = DescribeArgs<Args...>();
  if (parsed.arg_index >= bound_arguments.size())
  {
    return LoweredField{.error = Error::MissingArgument};
  }

  auto argument = bound_arguments[parsed.arg_index];
  switch (argument.kind)
  {
    case BoundKind::Bool:
      return LowerBool(parsed);
    case BoundKind::Character:
      return LowerCharacter(parsed);
    case BoundKind::Signed:
      return LowerIntegerLike(parsed, true, argument.uses_64bit_storage);
    case BoundKind::Unsigned:
      return LowerIntegerLike(parsed, false, argument.uses_64bit_storage);
    case BoundKind::String:
      return LowerString(parsed);
    case BoundKind::Pointer:
      return LowerPointer(parsed);
    case BoundKind::Float32:
    case BoundKind::Float64:
    case BoundKind::LongDouble:
      return LowerFloat(parsed, argument.kind);
    case BoundKind::Unsupported:
    default:
      return LoweredField{.error = Error::UnsupportedArgumentType};
  }
}
}  // namespace ArgumentBinding

/**
 * @brief Runs source-only analysis for one brace-style literal.
 * @brief 对单条 brace 风格字面量执行仅源串分析。
 */
template <Text Source>
[[nodiscard]] consteval auto Analyze()
{
  return SourceSyntax::Analyze<Source>();
}

/**
 * @brief Walks one brace-style literal and lowers each parsed field against
 *        the concrete C++ argument list.
 * @brief 遍历一条 brace 风格字面量，并按具体 C++ 参数列表对每个字段做降级。
 */
template <Text Source, typename... Args>
[[nodiscard]] consteval Error WalkAndLower(auto& visitor)
{
  struct LoweringVisitor
  {
    decltype(visitor)& inner;

    [[nodiscard]] consteval Error Text(size_t offset, size_t text_size)
    {
      return inner.Text(offset, text_size);
    }

    [[nodiscard]] consteval Error Field(const ParsedField& parsed)
    {
      auto lowered = ArgumentBinding::LowerField<Args...>(parsed);
      if (lowered.error != Error::None)
      {
        return lowered.error;
      }
      return inner.Field(lowered.field);
    }
  };

  LoweringVisitor lowering{visitor};
  return SourceSyntax::WalkSource(std::string_view(Source.Data(), Source.Size()),
                                  lowering);
}

/**
 * @brief Frontend adapter that binds one brace-style source literal to one
 *        concrete C++ argument list.
 * @brief 将一条 brace 风格源字面量绑定到一组具体 C++ 参数类型上的前端适配器。
 */
template <Text Source, typename... Args>
class Compiler
{
 public:
  using ErrorType = Error;

  [[nodiscard]] static constexpr const char* SourceData() { return Source.Data(); }
  [[nodiscard]] static constexpr size_t SourceSize() { return Source.Size(); }

  [[nodiscard]] static consteval ErrorType Walk(auto& visitor)
  {
    return WalkAndLower<Source, Args...>(visitor);
  }
};
}  // namespace LibXR::Print::Detail::FormatFrontend
