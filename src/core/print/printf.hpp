#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "format_argument.hpp"

namespace LibXR::Print
{
/**
 * @brief Structural literal wrapper used as the NTTP source for printf formats.
 * @brief 作为 printf 格式源的结构化字符串字面量包装。
 */
template <size_t N>
struct Text
{
  /// Literal bytes including the terminating zero byte. / 含结尾零字节的字面量字节序列
  char data[N]{};

  /**
   * @brief Copies the string literal into the structural NTTP object.
   * @brief 将字符串字面量复制到结构化 NTTP 对象中。
   */
  constexpr Text(const char (&text)[N])
  {
    for (size_t i = 0; i < N; ++i)
    {
      data[i] = text[i];
    }
  }

  /// Returns the format length without the terminating zero byte. / 返回不含结尾零字节的格式串长度
  [[nodiscard]] constexpr size_t Size() const { return N - 1; }
  /// Returns the literal bytes including the terminating zero byte. / 返回含结尾零字节的字面量指针
  [[nodiscard]] constexpr const char* Data() const { return data; }
};

/**
 * @brief Printf-style frontend that emits the internal format representation.
 * @brief printf 风格前端，输出内部格式表示。
 */
class Printf
{
 public:
  /**
   * @brief Supported printf length modifiers after normalization.
   * @brief 归一化后的 printf 长度修饰符。
   */
  enum class Length : uint8_t
  {
    Default,     ///< no length modifier / 无长度修饰符
    Char,        ///< hh / char 长度修饰
    Short,       ///< h / short 长度修饰
    Long,        ///< l / long 长度修饰
    LongLong,    ///< ll / long long 长度修饰
    IntMax,      ///< j / intmax_t 长度修饰
    Size,        ///< z / size_t 长度修饰
    PtrDiff,     ///< t / ptrdiff_t 长度修饰
    LongDouble,  ///< L / long double 长度修饰
  };

  /**
   * @brief Compile-time parse/build failure categories surfaced through static_assert.
   * @brief 通过 static_assert 暴露的编译期解析/构建失败类别。
   */
  enum class Error : uint8_t
  {
    None,                ///< success / 成功
    NumberOverflow,      ///< width / precision literal does not fit in its field / 宽度或精度字面量超出字段范围
    UnexpectedEnd,       ///< format string ended in the middle of one conversion / 格式串在转换项中途结束
    EmbeddedNul,         ///< format literal contains an embedded NUL byte / 格式串字面量内部包含嵌入式 NUL 字节
    MixedIndexing,       ///< positional and sequential arguments were mixed / 混用了位置参数与顺序参数
    PositionalArgumentDisabled,  ///< positional n$ indexing is disabled by configuration / 位置参数 n$ 索引已被配置关闭
    DynamicField,        ///< * width / precision is not supported / 不支持 * 宽度或精度
    InvalidArgumentIndex,  ///< positional argument index is invalid / 位置参数索引非法
    InvalidSpecifier,    ///< unsupported or disabled conversion specifier / 转换说明符无效或被禁用
    InvalidLength,       ///< length modifier is incompatible with the conversion / 长度修饰符与转换说明不兼容
    ConflictingArgument,  ///< one positional argument was reused with incompatible rules / 同一位置参数被不兼容的规则重复使用
    TextOffsetOverflow,  ///< referenced text offset no longer fits in uint16_t / 文本池偏移超出 uint16_t
    TextSizeOverflow,    ///< referenced text size no longer fits in uint16_t / 文本长度超出 uint16_t
  };

  template <Text Source>
  class Compiler;

  template <Text Source>
  struct Compiled;

  /**
   * @brief Parses and validates a printf format at compile time.
   * @brief 在编译期解析并校验 printf 格式串。
   */
  template <Text Source>
  [[nodiscard]] static consteval Compiled<Source> Build()
  {
    return {};
  }

  /**
   * @brief Returns true when Args... exactly match the enabled conversions.
   * @brief 当 Args... 与启用的转换项精确匹配时返回 true。
   */
  template <Text Source, typename... Args>
  [[nodiscard]] static consteval bool Matches()
  {
    return Compiled<Source>::template Matches<Args...>();
  }
};
}  // namespace LibXR::Print

#include "printf_frontend_detail.hpp"
namespace LibXR::Print
{
template <Text Source>
struct Printf::Compiled
{
 private:
  using Frontend = typename Printf::template Compiler<Source>;
  inline static constexpr auto source_analysis = Detail::PrintfCompile::Analyze<Source>();
  inline static constexpr auto result = FormatCompiler<Frontend>::Compile();

  static_assert(result.compile_error != Printf::Error::NumberOverflow,
                "LibXR::Print::Printf: numeric field is too large");
  static_assert(result.compile_error != Printf::Error::UnexpectedEnd,
                "LibXR::Print::Printf: unexpected end of format string");
  static_assert(result.compile_error != Printf::Error::EmbeddedNul,
                "LibXR::Print::Printf: embedded NUL bytes are not supported in the format literal");
  static_assert(result.compile_error != Printf::Error::MixedIndexing,
                "LibXR::Print::Printf: positional and sequential arguments cannot be mixed");
  static_assert(result.compile_error != Printf::Error::PositionalArgumentDisabled,
                "LibXR::Print::Printf: positional argument indexing is disabled in the current profile");
  static_assert(result.compile_error != Printf::Error::DynamicField,
                "LibXR::Print::Printf: dynamic width and precision are not supported");
  static_assert(result.compile_error != Printf::Error::InvalidArgumentIndex,
                "LibXR::Print::Printf: invalid positional argument index");
  static_assert(result.compile_error != Printf::Error::InvalidSpecifier,
                "LibXR::Print::Printf: invalid format specifier");
  static_assert(result.compile_error != Printf::Error::InvalidLength,
                "LibXR::Print::Printf: invalid length modifier");
  static_assert(result.compile_error != Printf::Error::ConflictingArgument,
                "LibXR::Print::Printf: one positional argument is reused with incompatible conversions");
  static_assert(result.compile_error != Printf::Error::TextOffsetOverflow,
                "LibXR::Print::Printf: text pool offset is too large");
  static_assert(result.compile_error != Printf::Error::TextSizeOverflow,
                "LibXR::Print::Printf: text span is too large");
  static_assert(source_analysis.error != Printf::Error::ConflictingArgument,
                "LibXR::Print::Printf: one positional argument is reused with incompatible conversions");

 public:
  /// Final runtime byte block kept by the compiled surface. / 编译格式表面保留的最终运行期字节块
  inline static constexpr auto codes = result.codes;
  /// Compile-time executor profile used to specialize the runtime bytecode interpreter. / 用于特化运行期字节码解释器的编译期执行器配置
  inline static constexpr FormatProfile profile = result.profile;

  /// Field-ordered argument metadata used by runtime argument packing. / 按字段顺序排列、供运行期参数打包使用的元信息表
  [[nodiscard]] static constexpr auto ArgumentList()
  {
    return result.arg_info;
  }

  /// Field-ordered source argument references, including duplicates and reordering. / 按字段顺序排列的源参数引用，可包含重复与重排
  [[nodiscard]] static constexpr auto ArgumentOrder()
  {
    return source_analysis.order;
  }

  /// Source-ordered argument metadata used only by compile-time type matching. / 按源参数顺序排列、仅供编译期类型匹配使用的元信息
  [[nodiscard]] static constexpr auto SourceArgumentList()
  {
    return source_analysis.args;
  }

  /// Final compiled bytes consumed directly by the runtime writer. / 供运行期 writer 直接消费的最终编译字节块
  [[nodiscard]] static constexpr const auto& Codes()
  {
    return codes;
  }

  /// Returns the compile-time executor profile. / 返回编译期执行器配置
  [[nodiscard]] static constexpr FormatProfile Profile()
  {
    return profile;
  }

  /// Returns true when Args... exactly match this compiled format. / 判断 Args... 是否与当前编译格式完全匹配
  template <typename... Args>
  [[nodiscard]] static consteval bool Matches()
  {
    return Detail::FormatArgument::template Matches<Args...>(SourceArgumentList());
  }
};
}  // namespace LibXR::Print
