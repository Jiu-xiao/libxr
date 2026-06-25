#pragma once

/**
 * @brief brace 前端的编译期分析与降级入口层 / Brace frontend compile-time analysis and lowering entry surface
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include "../format_compile.hpp"
#include "../format_argument.hpp"
#include "../printf.hpp"

namespace LibXR::Print::Detail::FormatFrontend
{
/**
 * @brief brace 风格 format 前端的编译期失败类别。 / Compile-time failure categories for the brace-style format frontend.
 */
enum class Error : uint8_t
{
  None,                    ///< success / 成功
  NumberOverflow,          ///< index / width / precision does not fit its target field / 索引、宽度或精度超出目标字段容量
  FloatPrecisionLimitExceeded,  ///< float precision exceeds the configured frontend limit / 浮点精度超出当前配置的前端上限
  UnexpectedEnd,           ///< source ended in the middle of one field / 字段尚未结束时源串已结束
  EmbeddedNul,             ///< embedded NUL before the terminator / 终止字节之前出现嵌入式 NUL
  UnmatchedBrace,          ///< unmatched { or } / 花括号未正确配对
  MixedIndexing,           ///< automatic and manual indexing were mixed / 混用了自动索引与手动索引
  ManualIndexingDisabled,  ///< explicit {0}/{1} argument indexing is disabled by configuration / 显式 {0}/{1} 参数索引已被配置关闭
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
 * @brief 降为 FormatFlag 位之前的已解析对齐方式。 / Parsed alignment directive before lowering into FormatFlag bits.
 */
enum class Align : uint8_t
{
  None,    ///< default alignment / 默认对齐
  Left,    ///< '<' / 左对齐
  Right,   ///< '>' / 右对齐
  Center,  ///< '^' / 居中对齐
};

/**
 * @brief 在绑定到具体 C++ 参数类型之前的 brace 字段解析结果。 / Parsed brace field before binding it to one concrete C++ argument type.
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
 * @brief 前端侧的参数类别，用来选择字段该走哪条解析路径。 / Frontend-side argument categories used to choose one field-resolution path.
 */
enum class ArgumentKind : uint8_t
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
 * @brief 单个 C++ 参数在前端里的类别摘要，以及对应的宽度策略。 / Frontend summary of one C++ argument category plus width policy.
 */
struct ArgumentSummary
{
  ArgumentKind kind = ArgumentKind::Unsupported;  ///< frontend-side argument category / 前端侧参数类别
  bool uses_64bit_storage = false;          ///< whether integer storage must be 64-bit / 整数是否必须走 64 位存储
};

/**
 * @brief 将单个已解析 brace 字段解析成共享格式协议后的结果。 / Result of resolving one parsed brace field into the shared format protocol.
 */
struct ResolvedField
{
  Error error = Error::None;  ///< field-resolution result / 字段解析结果
  FormatField field{};        ///< resolved shared field / 解析后的共享字段
};

#include "format_frontend_source.hpp"

#include "format_frontend_binding_base.hpp"
#include "format_frontend_binding_integer.hpp"
#include "format_frontend_binding_float.hpp"

/**
 * @brief 对单条 brace 风格字面量执行仅源串分析 / Run source-only analysis for one brace-style literal
 * @tparam Source brace 风格格式串字面量 / Brace-style format literal
 * @return 源串分析结果，包含字段顺序、所需参数个数与首个源级错误 / Returns the source analysis result, including field order, required argument count, and the first source-level error
 */
template <Text Source>
[[nodiscard]] consteval auto Analyze()
{
  return SourceSyntax::Analyze<Source>();
}

/**
 * @brief 遍历一条 brace 风格字面量，并针对具体 C++ 参数列表产出共享 `FormatField` 记录 / Walk one brace-style literal and emit shared `FormatField` records selected for the concrete C++ argument list
 * @tparam Source brace 风格格式串字面量 / Brace-style format literal
 * @tparam Args 这次绑定使用的 C++ 实参类型列表 / Concrete C++ argument types used for this binding
 * @param visitor 接收文本片段与最终 `FormatField` 记录的 visitor / Visitor receiving text spans and final `FormatField` records
 * @return 首个源级错误或字段解析错误；成功时返回 `Error::None` / Returns the first source-level or field-resolution error; returns `Error::None` on success
 */
template <Text Source, typename... Args>
[[nodiscard]] consteval Error WalkSourceAsFormatFields(auto& visitor)
{
  struct ResolvingVisitor
  {
    decltype(visitor)& inner;

    [[nodiscard]] consteval Error Text(size_t offset, size_t text_size)
    {
      return inner.Text(offset, text_size);
    }

    [[nodiscard]] consteval Error Field(const ParsedField& parsed)
    {
      auto resolved = ArgumentResolution::ResolveField<Args...>(parsed);
      if (resolved.error != Error::None)
      {
        return resolved.error;
      }
      return inner.Field(resolved.field);
    }
  };

  ResolvingVisitor resolving{visitor};
  return SourceSyntax::WalkSource(std::string_view(Source.Data(), Source.Size()),
                                  resolving);
}

/**
 * @brief 将一条 brace 风格源字面量绑定到一组具体 C++ 参数类型上的前端适配器 / Frontend adapter that binds one brace-style source literal to one concrete C++ argument list
 * @tparam Source brace 风格格式串字面量 / Brace-style format literal
 * @tparam Args 这次绑定使用的 C++ 实参类型列表 / Concrete C++ argument types used for this binding
 */
template <Text Source, typename... Args>
class Compiler
{
 public:
  using ErrorType = Error;

  /**
   * @brief 返回不含结尾零字节的源字符串字节序列 / Return the source string bytes without a terminating zero byte
   * @return 指向当前格式字面量正文的指针 / Returns a pointer to the literal body
   */
  [[nodiscard]] static constexpr const char* SourceData() { return Source.Data(); }
  /**
   * @brief 返回不含结尾零字节的源字符串长度 / Return the source string length without a terminating zero byte
   * @return 当前格式字面量正文长度 / Returns the literal-body size
   */
  [[nodiscard]] static constexpr size_t SourceSize() { return Source.Size(); }

  /**
   * @brief 遍历当前已绑定前端，并产出文本片段和最终 `FormatField` 记录 / Walk this bound frontend and emit text spans plus final `FormatField` records
   * @param visitor 接收文本片段与最终字段记录的 visitor / Visitor receiving text spans and final field records
   * @return 首个源级错误或字段解析错误；成功时返回 `Error::None` / Returns the first source-level or field-resolution error; returns `Error::None` on success
   */
  [[nodiscard]] static consteval ErrorType Walk(auto& visitor)
  {
    return WalkSourceAsFormatFields<Source, Args...>(visitor);
  }
};
}  // namespace LibXR::Print::Detail::FormatFrontend
