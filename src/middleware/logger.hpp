#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "libxr_color.hpp"
#include "libxr_rw.hpp"
#include "libxr_time.hpp"

namespace LibXR
{

/**
 * @enum LogLevel
 * @brief 日志级别枚举 / Log level enumeration
 */
enum class LogLevel : uint8_t
{
  XR_LOG_LEVEL_DEBUG = 0,  ///< 调试信息 / Debug message
  XR_LOG_LEVEL_INFO,       ///< 一般信息 / Informational message
  XR_LOG_LEVEL_PASS,       ///< 通过信息 / Pass message
  XR_LOG_LEVEL_WARN,       ///< 警告信息 / Warning message
  XR_LOG_LEVEL_ERROR,      ///< 错误信息 / Error message
};

/**
 * @struct LogData
 * @brief 日志数据结构体 / Log data structure
 */
struct LogData
{
  TimestampMS timestamp;                 ///< 时间戳 / Timestamp
  LogLevel level;                        ///< 日志级别 / Log level
  const char* file;                      ///< 文件名指针 / Source file name pointer
  uint32_t line;                         ///< 行号 / Line number
  char message[XR_LOG_MESSAGE_MAX_LEN];  ///< 日志消息内容 / Log message content
};

/**
 * @class Logger
 * @brief LibXR 日志管理器 / LibXR Logger Manager
 */
class Logger
{
 public:
  /**
   * @brief 初始化日志主题 / Initialize the log topic
   */
  static void Init();

  /**
   * @brief 发布一条日志 / Publish a log message
   * @param level 日志级别 / Log level
   * @param file 来源文件名 / Source file name
   * @param line 行号 / Line number
   * @param fmt 格式化字符串 / Format string
   * @param ... 可变参数 / Variable arguments
   */
  // NOLINTNEXTLINE
  static void Publish(LogLevel level, const char* file, uint32_t line, const char* fmt,
                      ...);

 private:
  /**
   * @brief 打印日志到终端 / Print log to terminal
   * @param data 日志数据 / Log data
   */
  static void PrintToTerminal(const LogData& data)
  {
    const char* color = GetColor(data.level);

    STDIO::Printf("%s[%u][%s](%s:%u) %s%s\r\n", color,
                  static_cast<uint32_t>(data.timestamp), LevelToString(data.level),
                  data.file, data.line, data.message,
                  LIBXR_FORMAT_STR[static_cast<uint8_t>(Format::RESET)]);
  }

  /**
   * @brief 根据日志级别获取显示颜色 / Get color code based on log level
   * @param level 日志级别 / Log level
   * @return 颜色字符串 / Color string
   */
  static const char* GetColor(LogLevel level)
  {
    using namespace LibXR;
    switch (level)
    {
      case LogLevel::XR_LOG_LEVEL_DEBUG:
        return LIBXR_FONT_STR[static_cast<uint8_t>(Font::MAGENTA)];
      case LogLevel::XR_LOG_LEVEL_INFO:
        return LIBXR_FONT_STR[static_cast<uint8_t>(Font::CYAN)];
      case LogLevel::XR_LOG_LEVEL_PASS:
        return LIBXR_FONT_STR[static_cast<uint8_t>(Font::GREEN)];
      case LogLevel::XR_LOG_LEVEL_WARN:
        return LIBXR_FONT_STR[static_cast<uint8_t>(Font::YELLOW)];
      case LogLevel::XR_LOG_LEVEL_ERROR:
        return LIBXR_FONT_STR[static_cast<uint8_t>(Font::RED)];
      default:
        return "";
    }
  }

  /**
   * @brief 将日志级别转换为字符串 / Convert log level to string
   * @param level 日志级别 / Log level
   * @return 日志级别字符串 / Log level string
   */
  static const char* LevelToString(LogLevel level)
  {
    switch (level)
    {
      case LogLevel::XR_LOG_LEVEL_DEBUG:
        return "DEBUG";
      case LogLevel::XR_LOG_LEVEL_INFO:
        return "INFO";
      case LogLevel::XR_LOG_LEVEL_PASS:
        return "PASS";
      case LogLevel::XR_LOG_LEVEL_WARN:
        return "WARN";
      case LogLevel::XR_LOG_LEVEL_ERROR:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
  }

  static inline std::atomic<bool> initialized_ = false;
};

}  // namespace LibXR

#if LIBXR_LOG_LEVEL >= 4
/**
 * @brief 输出调试日志 / Output debug log
 */
#define XR_LOG_DEBUG(fmt, ...)                                                         \
  LibXR::Logger::Publish(LibXR::LogLevel::XR_LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, \
                         ##__VA_ARGS__)
#else
#define XR_LOG_DEBUG(...)
#endif

#if LIBXR_LOG_LEVEL >= 3
/**
 * @brief 输出一般信息日志 / Output info log
 */
#define XR_LOG_INFO(fmt, ...)                                                         \
  LibXR::Logger::Publish(LibXR::LogLevel::XR_LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, \
                         ##__VA_ARGS__)
#else
#define XR_LOG_INFO(...)
#endif

#if LIBXR_LOG_LEVEL >= 2
/**
 * @brief 输出通过测试日志 / Output pass log
 */
#define XR_LOG_PASS(fmt, ...)                                                         \
  LibXR::Logger::Publish(LibXR::LogLevel::XR_LOG_LEVEL_PASS, __FILE__, __LINE__, fmt, \
                         ##__VA_ARGS__)
#else
#define XR_LOG_PASS(...)
#endif

#if LIBXR_LOG_LEVEL >= 1
/**
 * @brief 输出警告日志 / Output warning log
 */
#define XR_LOG_WARN(fmt, ...)                                                         \
  LibXR::Logger::Publish(LibXR::LogLevel::XR_LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, \
                         ##__VA_ARGS__)
#else
#define XR_LOG_WARN(...)
#endif

#if LIBXR_LOG_LEVEL >= 0
/**
 * @brief 输出错误日志 / Output error log
 */
#define XR_LOG_ERROR(fmt, ...)                                                         \
  LibXR::Logger::Publish(LibXR::LogLevel::XR_LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, \
                         ##__VA_ARGS__)
#else
#define XR_LOG_ERROR(...)
#endif
