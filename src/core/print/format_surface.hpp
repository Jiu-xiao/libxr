#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "format_frontend_detail.hpp"
#include "writer.hpp"

namespace LibXR
{
template <Print::Text Source>
class Format
{
 private:
  using SourceError = Print::Detail::FormatFrontend::Error;
  inline static constexpr auto source_analysis =
      Print::Detail::FormatFrontend::Analyze<Source>();

  static_assert(source_analysis.error != SourceError::NumberOverflow,
                "LibXR::Format: index, width, or precision is too large");
  static_assert(source_analysis.error != SourceError::UnexpectedEnd,
                "LibXR::Format: unexpected end of format string");
  static_assert(source_analysis.error != SourceError::EmbeddedNul,
                "LibXR::Format: embedded NUL bytes are not supported in the format literal");
  static_assert(source_analysis.error != SourceError::UnmatchedBrace,
                "LibXR::Format: unmatched brace in format string");
  static_assert(source_analysis.error != SourceError::MixedIndexing,
                "LibXR::Format: automatic and manual argument indexing cannot be mixed");
  static_assert(source_analysis.error != SourceError::ManualIndexingDisabled,
                "LibXR::Format: explicit argument indexing is disabled in the current profile");
  static_assert(source_analysis.error != SourceError::DynamicField,
                "LibXR::Format: dynamic width and precision are not supported");
  static_assert(source_analysis.error != SourceError::InvalidArgumentIndex,
                "LibXR::Format: invalid argument index");
  static_assert(source_analysis.error != SourceError::InvalidSpecifier,
                "LibXR::Format: invalid format specifier");
  static_assert(source_analysis.error != SourceError::InvalidPresentation,
                "LibXR::Format: invalid presentation type");

 public:
  /**
   * @brief Public brace-style compile-time failure categories.
   * @brief 面向用户的 brace 风格编译期失败类别。
   */
  using Error = Print::Detail::FormatFrontend::Error;

  /**
   * @brief Frontend adapter bound to one concrete C++ argument list.
   * @brief 绑定到一组具体 C++ 参数类型上的前端适配器。
   */
  template <typename... Args>
  using Compiler = Print::Detail::FormatFrontend::Compiler<Source, Args...>;

  /**
   * @brief One brace-style source bound to a concrete call-site argument list.
   * @brief 一条 brace 风格源串绑定到具体调用点参数后的编译结果。
   */
  template <typename... Args>
  struct Compiled
  {
   private:
    using Frontend = Compiler<Args...>;
    inline static constexpr auto result = Print::FormatCompiler<Frontend>::Compile();

    static_assert(source_analysis.error != SourceError::None ||
                      sizeof...(Args) == source_analysis.required_argument_count,
                  "LibXR::Format: call-site argument count does not match the "
                  "referenced replacement fields");
    static_assert(result.compile_error != Error::NumberOverflow,
                  "LibXR::Format: index, width, or precision is too large");
    static_assert(result.compile_error != Error::UnexpectedEnd,
                  "LibXR::Format: unexpected end of format string");
    static_assert(result.compile_error != Error::EmbeddedNul,
                  "LibXR::Format: embedded NUL bytes are not supported in the format literal");
    static_assert(result.compile_error != Error::UnmatchedBrace,
                  "LibXR::Format: unmatched brace in format string");
    static_assert(result.compile_error != Error::MixedIndexing,
                  "LibXR::Format: automatic and manual argument indexing cannot be mixed");
    static_assert(result.compile_error != Error::ManualIndexingDisabled,
                  "LibXR::Format: explicit argument indexing is disabled in the current profile");
    static_assert(result.compile_error != Error::DynamicField,
                  "LibXR::Format: dynamic width and precision are not supported");
    static_assert(result.compile_error != Error::InvalidArgumentIndex,
                  "LibXR::Format: invalid argument index");
    static_assert(result.compile_error != Error::InvalidSpecifier,
                  "LibXR::Format: invalid format specifier");
    static_assert(result.compile_error != Error::InvalidPresentation,
                  "LibXR::Format: invalid presentation type");
    static_assert(result.compile_error != Error::MissingArgument,
                  "LibXR::Format: referenced argument index is out of range");
    static_assert(result.compile_error != Error::ArgumentTypeMismatch,
                  "LibXR::Format: format options are incompatible with the selected argument type");
    static_assert(result.compile_error != Error::UnsupportedArgumentType,
                  "LibXR::Format: unsupported C++ argument type");
    static_assert(result.compile_error != Error::TextOffsetOverflow,
                  "LibXR::Format: text pool offset is too large");
    static_assert(result.compile_error != Error::TextSizeOverflow,
                  "LibXR::Format: text span is too large");

   public:
    /// Final runtime byte block kept by the compiled surface. / 编译格式表面保留的最终运行期字节块
    inline static constexpr auto codes = result.codes;
    /// Compile-time executor profile used to specialize the runtime bytecode interpreter. / 用于特化运行期字节码解释器的编译期执行器配置
    inline static constexpr Print::FormatProfile profile = result.profile;

    /// Ordered field metadata used by runtime argument packing. / 按字段顺序排列、供运行期参数打包使用的元信息
    [[nodiscard]] static constexpr auto ArgumentList()
    {
      return result.arg_info;
    }

    /// Field-ordered source argument references, including duplicates and reordering. / 按字段顺序排列的源参数引用，可包含重复与重排
    [[nodiscard]] static constexpr auto ArgumentOrder()
    {
      return source_analysis.argument_order;
    }

    /// Final compiled bytes consumed directly by the runtime writer. / 供运行期 writer 直接消费的最终编译字节块
    [[nodiscard]] static constexpr const auto& Codes()
    {
      return codes;
    }

    /// Returns the compile-time executor profile. / 返回编译期执行器配置
    [[nodiscard]] static constexpr Print::FormatProfile Profile()
    {
      return profile;
    }

    /// Returns true when Actual... exactly match this bound compiled surface. / 判断 Actual... 是否与当前已绑定的编译格式完全一致
    template <typename... Actual>
    [[nodiscard]] static consteval bool Matches()
    {
      return std::is_same_v<std::tuple<std::remove_cvref_t<Actual>...>,
                            std::tuple<Args...>>;
    }
  };

  /// Returns the required call-site argument count addressed by this source. / 返回当前源串会寻址的调用点参数个数
  [[nodiscard]] static constexpr size_t ArgumentCount()
  {
    return source_analysis.required_argument_count;
  }

  /// Returns true when Args... are accepted by this brace frontend. / 判断 Args... 是否能被当前 brace 前端接受
  template <typename... Args>
  [[nodiscard]] static consteval bool Matches()
  {
    if constexpr (sizeof...(Args) != source_analysis.required_argument_count)
    {
      return false;
    }
    else
    {
      using Frontend = Compiler<std::remove_cvref_t<Args>...>;
      return Print::FormatCompiler<Frontend>::Compile().compile_error == Error::None;
    }
  }

  /// Writes this format into one sink and returns only the sink status. / 将当前格式写入一个输出端，并且只返回 sink 状态
  template <typename Sink, typename... Args>
  [[nodiscard]] ErrorCode WriteTo(Sink& sink, Args&&... args) const
  {
    using Built = Compiled<std::remove_cvref_t<Args>...>;
    return Print::Writer::template RunArgumentOrder<Sink, Built, Built::ArgumentOrder()>(
        sink, Built{}, std::forward<Args>(args)...);
  }
};
}  // namespace LibXR

namespace LibXR::Print
{
/**
 * @brief Writes one brace-style format wrapper by first binding it to the
 *        concrete Args... type list and then executing the shared runtime writer.
 * @brief 写入一份 brace 风格格式包装器：先将其绑定到具体 Args... 类型列表，再执行共享运行期 writer。
 */
template <Text Source, typename Sink, typename... Args>
[[nodiscard]] inline ErrorCode Write(Sink& sink, const LibXR::Format<Source>&,
                                     Args&&... args)
{
  using Built =
      typename LibXR::Format<Source>::template Compiled<std::remove_cvref_t<Args>...>;
  return Writer::template RunArgumentOrder<Sink, Built, Built::ArgumentOrder()>(
      sink, Built{}, std::forward<Args>(args)...);
}
}  // namespace LibXR::Print
