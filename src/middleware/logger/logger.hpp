#pragma once

#include <cstdint>
#include <utility>

#include "libxr_color.hpp"
#include "libxr_rw.hpp"
#include "libxr_time.hpp"

namespace LibXR
{

/**
 * @brief `logger` 对外包含入口 / Public include entry for `logger`
 *
 * @note 外部代码仍应优先包含这个头；字面量前端选择和日志宏定义已拆到同目录的内部片段。
 *       External code should still include this header first; literal-frontend
 *       selection and log macro definitions are split into internal fragments
 *       in the same directory.
 */

/**
 * @brief logger 字面量前端解析片段 / Logger literal-frontend resolution fragment
 */
#include "literal.hpp"

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
        Detail::LoggerLiteral::SelectFrontend<Detail::LoggerLiteral::Frontend::Auto,
                                              Source, Args...>();
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
  /**
   * @brief 按已确定前端发布一条日志
   *        Publish one log entry under the already selected frontend
   * @tparam FrontendMode 已选定的日志字面量前端 / Already selected literal frontend
   * @tparam Source 日志源串 / Log source literal
   * @tparam Args 格式参数类型 / Format argument types
   * @param level 日志级别 / Log level
   * @param file 来源文件名 / Source file name
   * @param line 行号 / Line number
   * @param args 格式参数 / Format arguments
   *
   * @note 这里负责惰性初始化日志主题，并把格式化后的文本写进固定 `LogData` 缓冲区；
   *       真正的 topic 发布留给 `PublishToTopic()`。
   *       This path performs lazy logger-topic initialization and formats the
   *       final text into the fixed `LogData` buffer; the actual topic publish
   *       is delegated to `PublishToTopic()`.
   */
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

  /**
   * @brief 把一条日志数据发布到内部日志 topic
   *        Publish one log record into the internal log topic
   * @param data 待发布日志 / Log record to publish
   */
  static void PublishToTopic(LogData& data);

  static inline bool initialized_ = false;  ///< 是否已经完成日志 topic 初始化 / Whether logger-topic initialization has completed.
};

}  // namespace LibXR

/**
 * @brief logger 宏表面片段 / Logger macro-surface fragment
 */
#include "macros.hpp"
