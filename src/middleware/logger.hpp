#pragma once

#include <cstdint>
#include <utility>

#include "libxr_color.hpp"
#include "libxr_rw.hpp"
#include "libxr_time.hpp"

namespace LibXR
{
namespace Detail::LoggerLiteral
{
/**
 * @brief Logger literal frontend selection mode.
 * @brief Logger 字面量前端选择模式。
 */
enum class Frontend : uint8_t
{
  Auto,    ///< select brace or printf automatically / 自动选择 brace 或 printf
  Format,  ///< force brace-style frontend / 强制使用 brace 风格前端
  Printf,  ///< force printf-style frontend / 强制使用 printf 风格前端
};

/**
 * @brief Result of resolving one logger literal against the available
 *        frontends.
 * @brief 将一条 logger 字面量与可用前端进行解析后的结果。
 */
enum class Resolution : uint8_t
{
  None,       ///< matches neither frontend / 两个前端都不匹配
  Format,     ///< select brace-style frontend / 选择 brace 风格前端
  Printf,     ///< select printf-style frontend / 选择 printf 风格前端
  Ambiguous,  ///< both frontends remain valid and both syntaxes are used / 两个前端都可用且都真的使用了自己的语法
};

/**
 * @brief Returns whether one valid brace literal actually uses brace syntax.
 * @brief 判断一条合法 brace 字面量是否真的使用了 brace 语法。
 */
template <Print::Text Source>
[[nodiscard]] consteval bool UsesFormatSyntax()
{
  for (size_t i = 0; i < Source.Size(); ++i)
  {
    if (Source.data[i] == '{' || Source.data[i] == '}')
    {
      return true;
    }
  }
  return false;
}

/**
 * @brief Returns whether one valid printf literal actually uses printf syntax.
 * @brief 判断一条合法 printf 字面量是否真的使用了 printf 语法。
 */
template <Print::Text Source>
[[nodiscard]] consteval bool UsesPrintfSyntax()
{
  for (size_t i = 0; i < Source.Size(); ++i)
  {
    if (Source.data[i] == '%')
    {
      return true;
    }
  }
  return false;
}

/**
 * @brief Returns whether one brace-style source is source-level valid.
 * @brief 判断一条 brace 风格源串在源级上是否合法。
 */
template <Print::Text Source>
[[nodiscard]] consteval bool FormatSourceValid()
{
  return Print::Detail::FormatFrontend::Analyze<Source>().error ==
         Print::Detail::FormatFrontend::Error::None;
}

/**
 * @brief Returns whether one printf-style source is source-level valid.
 * @brief 判断一条 printf 风格源串在源级上是否合法。
 */
template <Print::Text Source>
[[nodiscard]] consteval bool PrintfSourceValid()
{
  return Print::Detail::PrintfCompile::Analyze<Source>().error == Print::Printf::Error::None;
}

/**
 * @brief Returns whether one argument list is accepted by the brace frontend,
 *        guarded by source-level validity first.
 * @brief 判断一组参数是否能被 brace 前端接受；会先做源级合法性保护。
 *
 * Logger auto-detection must not treat extra call-site arguments as harmless
 * for brace literals, otherwise unsupported printf-like sources can fall back
 * to brace plain text and silently drop their arguments.
 * logger 自动检测不能把多余实参当作 brace 字面量的无害输入，否则不受支持的
 * printf 风格源串可能回退成 brace 纯文本并静默丢弃实参。
 */
template <Print::Text Source, typename... Args>
[[nodiscard]] consteval bool FormatMatches()
{
  if constexpr (!FormatSourceValid<Source>())
  {
    return false;
  }
  else
  {
    return LibXR::Format<Source>::template Matches<Args...>();
  }
}

/**
 * @brief Returns whether one argument list is accepted by the printf frontend,
 *        guarded by source-level validity first.
 * @brief 判断一组参数是否能被 printf 前端接受；会先做源级合法性保护。
 */
template <Print::Text Source, typename... Args>
[[nodiscard]] consteval bool PrintfMatches()
{
  if constexpr (!PrintfSourceValid<Source>())
  {
    return false;
  }
  else
  {
    return Print::Printf::template Matches<Source, Args...>();
  }
}

/**
 * @brief Selects the logger frontend for one literal plus one concrete
 *        argument list.
 * @brief 为一条 logger 字面量及一组具体参数选择前端。
 */
template <Frontend Forced, Print::Text Source, typename... Args>
[[nodiscard]] consteval Resolution ResolveFrontend()
{
  constexpr bool format_match = FormatMatches<Source, Args...>();
  constexpr bool printf_match = PrintfMatches<Source, Args...>();

  if constexpr (Forced == Frontend::Format)
  {
    return format_match ? Resolution::Format : Resolution::None;
  }
  else if constexpr (Forced == Frontend::Printf)
  {
    return printf_match ? Resolution::Printf : Resolution::None;
  }
  else if constexpr (format_match && !printf_match)
  {
    return Resolution::Format;
  }
  else if constexpr (!format_match && printf_match)
  {
    return Resolution::Printf;
  }
  else if constexpr (!format_match && !printf_match)
  {
    return Resolution::None;
  }
  else
  {
    constexpr bool format_uses_syntax = UsesFormatSyntax<Source>();
    constexpr bool printf_uses_syntax = UsesPrintfSyntax<Source>();

    if constexpr (format_uses_syntax && !printf_uses_syntax)
    {
      return Resolution::Format;
    }
    else if constexpr (!format_uses_syntax && printf_uses_syntax)
    {
      return Resolution::Printf;
    }
    else if constexpr (!format_uses_syntax && !printf_uses_syntax)
    {
      return Resolution::Format;
    }
    else
    {
      return Resolution::Ambiguous;
    }
  }
}

/**
 * @brief Selects the final logger frontend after validating the resolution
 *        result.
 * @brief 在校验解析结果后，选择最终 logger 前端。
 */
template <Frontend Forced, Print::Text Source, typename... Args>
[[nodiscard]] consteval Frontend SelectFrontend()
{
  constexpr auto resolution = ResolveFrontend<Forced, Source, Args...>();

  if constexpr (Forced == Frontend::Format)
  {
    static_assert(resolution == Resolution::Format,
                  "LibXR::Logger: XR_FMT(...) literal is not accepted by the brace frontend");
    return Frontend::Format;
  }
  else if constexpr (Forced == Frontend::Printf)
  {
    static_assert(resolution == Resolution::Printf,
                  "LibXR::Logger: XR_PRINTF(...) literal is not accepted by the printf frontend");
    return Frontend::Printf;
  }
  else if constexpr (resolution == Resolution::Format)
  {
    return Frontend::Format;
  }
  else if constexpr (resolution == Resolution::Printf)
  {
    return Frontend::Printf;
  }
  else if constexpr (resolution == Resolution::Ambiguous)
  {
    static_assert(resolution != Resolution::Ambiguous,
                  "LibXR::Logger: literal is ambiguous between brace and printf frontends; use XR_FMT(...) or XR_PRINTF(...)");
    return Frontend::Auto;
  }
  else
  {
    static_assert(resolution != Resolution::None,
                  "LibXR::Logger: literal matches neither brace nor printf frontend");
    return Frontend::Auto;
  }
}
}  // namespace Detail::LoggerLiteral

/**
 * @enum LogLevel
 * @brief 日志级别枚举 / Log level enumeration
 */
enum class LogLevel : uint8_t
{
  XR_LOG_LEVEL_ERROR = 0,  ///< 错误信息 / Error message
  XR_LOG_LEVEL_WARN = 1,   ///< 警告信息 / Warning message
  XR_LOG_LEVEL_PASS = 2,   ///< 通过信息 / Pass message
  XR_LOG_LEVEL_INFO = 3,   ///< 一般信息 / Informational message
  XR_LOG_LEVEL_DEBUG = 4,  ///< 调试信息 / Debug message
};

/**
 * @struct LogData
 * @brief 日志数据结构体 / Log data structure
 */
struct LogData
{
  LogLevel level;                        ///< 日志级别 / Log level
  const char* file;                      ///< 文件名指针 / Source file name pointer
  uint32_t line;                         ///< 行号 / Line number
  char message[XR_LOG_MESSAGE_MAX_LEN];  ///< 日志消息内容 / Log message content
};

/**
 * @class Logger
 * @brief 日志管理器 / LibXR Logger Manager
 */
class Logger
{
 public:
  /**
   * @brief 初始化日志主题 / Initialize the log topic
   */
  static void Init();

  /**
   * @brief 发布一条字面量日志 / Publish one literal log message
   * @param level 日志级别 / Log level
   * @param file 来源文件名 / Source file name
   * @param line 行号 / Line number
   * @param args 格式参数 / Format arguments
   */
  template <Print::Text Source, typename... Args>
  static void Publish(LogLevel level, const char* file, uint32_t line, Args&&... args)
  {
    constexpr auto frontend =
        Detail::LoggerLiteral::SelectFrontend<Detail::LoggerLiteral::Frontend::Auto, Source,
                                              Args...>();
    PublishSelected<frontend, Source>(level, file, line, std::forward<Args>(args)...);
  }

  /**
   * @brief 发布一条带显式前端标签的字面量日志 / Publish one literal log message
   *        with an explicit frontend tag
   * @param level 日志级别 / Log level
   * @param file 来源文件名 / Source file name
   * @param line 行号 / Line number
   * @param args 格式参数 / Format arguments
   */
  template <Detail::LoggerLiteral::Frontend Forced, Print::Text Source, typename... Args>
  static void Publish(LogLevel level, const char* file, uint32_t line, Args&&... args)
  {
    static_assert(Forced != Detail::LoggerLiteral::Frontend::Auto,
                  "LibXR::Logger: explicit literal tag must be XR_FMT(...) or XR_PRINTF(...)");

    constexpr auto frontend =
        Detail::LoggerLiteral::SelectFrontend<Forced, Source, Args...>();
    PublishSelected<frontend, Source>(level, file, line, std::forward<Args>(args)...);
  }

 private:
  template <Detail::LoggerLiteral::Frontend FrontendMode, Print::Text Source,
            typename... Args>
  static void PublishSelected(LogLevel level, const char* file, uint32_t line,
                              Args&&... args)
  {
    if (!initialized_)
    {
      Init();
    }

    LogData data;
    data.level = level;
    data.file = file;
    data.line = line;

    if constexpr (FrontendMode == Detail::LoggerLiteral::Frontend::Format)
    {
      constexpr LibXR::Format<Source> format{};
      auto written = Print::FormatIntoBuffer(data.message, sizeof(data.message), format,
                                             std::forward<Args>(args)...);
      UNUSED(written);
    }
    else
    {
      constexpr auto format = Print::Printf::Build<Source>();
      auto written = Print::FormatIntoBuffer(data.message, sizeof(data.message), format,
                                             std::forward<Args>(args)...);
      UNUSED(written);
    }

    PublishToTopic(data);
  }

  static void PublishToTopic(LogData& data);
  static inline bool initialized_ = false;
};

}  // namespace LibXR

/**
 * @brief 显式指定 logger 使用 brace 风格前端 / Explicitly force the logger brace frontend
 */
#define XR_FMT(fmt) LibXR::Detail::LoggerLiteral::Frontend::Format, fmt

/**
 * @brief 显式指定 logger 使用 printf 风格前端 / Explicitly force the logger printf frontend
 */
#define XR_PRINTF(fmt) LibXR::Detail::LoggerLiteral::Frontend::Printf, fmt

#if LIBXR_LOG_LEVEL >= 4
/**
 * @brief 输出调试日志 / Output debug log
 */
#define XR_LOG_DEBUG(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_DEBUG, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_DEBUG(...)
#endif

#if LIBXR_LOG_LEVEL >= 3
/**
 * @brief 输出一般信息日志 / Output info log
 */
#define XR_LOG_INFO(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_INFO, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_INFO(...)
#endif

#if LIBXR_LOG_LEVEL >= 2
/**
 * @brief 输出通过测试日志 / Output pass log
 */
#define XR_LOG_PASS(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_PASS, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_PASS(...)
#endif

#if LIBXR_LOG_LEVEL >= 1
/**
 * @brief 输出警告日志 / Output warning log
 */
#define XR_LOG_WARN(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_WARN, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_WARN(...)
#endif

#if LIBXR_LOG_LEVEL >= 0
/**
 * @brief 输出错误日志 / Output error log
 */
#define XR_LOG_ERROR(fmt, ...)                                                         \
  LibXR::Logger::Publish<fmt>(LibXR::LogLevel::XR_LOG_LEVEL_ERROR, __FILE__, __LINE__, \
                              ##__VA_ARGS__)
#else
#define XR_LOG_ERROR(...)
#endif
