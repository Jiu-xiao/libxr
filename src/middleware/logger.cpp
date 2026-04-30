#include "logger.hpp"

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"

using namespace LibXR;

namespace
{
static Topic log_topic;  ///< 日志发布主题 / Log publish topic

const char* GetLogColor(LogLevel level)
{
  switch (level)
  {
    case LogLevel::XR_LOG_LEVEL_DEBUG:
      return LIBXR_FOREGROUND_STR[static_cast<uint8_t>(Foreground::MAGENTA)];
    case LogLevel::XR_LOG_LEVEL_INFO:
      return LIBXR_FOREGROUND_STR[static_cast<uint8_t>(Foreground::CYAN)];
    case LogLevel::XR_LOG_LEVEL_PASS:
      return LIBXR_FOREGROUND_STR[static_cast<uint8_t>(Foreground::GREEN)];
    case LogLevel::XR_LOG_LEVEL_WARN:
      return LIBXR_FOREGROUND_STR[static_cast<uint8_t>(Foreground::YELLOW)];
    case LogLevel::XR_LOG_LEVEL_ERROR:
      return LIBXR_FOREGROUND_STR[static_cast<uint8_t>(Foreground::RED)];
    default:
      return "";
  }
}

const char* LogLevelToString(LogLevel level)
{
  switch (level)
  {
    case LogLevel::XR_LOG_LEVEL_DEBUG:
      return "D";
    case LogLevel::XR_LOG_LEVEL_INFO:
      return "I";
    case LogLevel::XR_LOG_LEVEL_PASS:
      return "P";
    case LogLevel::XR_LOG_LEVEL_WARN:
      return "W";
    case LogLevel::XR_LOG_LEVEL_ERROR:
      return "E";
    default:
      return "UNKNOWN";
  }
}

void PrintLogToTerminal(const LogData& data, MicrosecondTimestamp timestamp)
{
  const char* color = GetLogColor(data.level);
  const uint32_t timestamp_ms =
      static_cast<uint32_t>(static_cast<uint64_t>(timestamp) / 1000U);
  STDIO::Print<"{}{} [{}]({}:{}) {}{}\r\n">(
      color, LogLevelToString(data.level), timestamp_ms, data.file, data.line,
      data.message, LIBXR_STYLE_STR[static_cast<uint8_t>(Style::RESET)]);
}

void OnLogMessage(bool, Topic tp, const Topic::MessageView<LogData>& log_message)
{
  UNUSED(tp);

  if (LIBXR_LOG_OUTPUT_LEVEL >= static_cast<uint8_t>(log_message.data.level) &&
      STDIO::write_ && STDIO::write_->Writable())
  {
    PrintLogToTerminal(log_message.data, log_message.timestamp);
  }
}
}  // namespace

void Logger::Init()
{
  log_topic = Topic::CreateTopic<LogData>("/xr/log", nullptr, true, false, false);

  auto log_cb = LibXR::Topic::Callback::Create(OnLogMessage, log_topic);
  log_topic.RegisterCallback(log_cb);

  initialized_ = true;
}

void Logger::PublishToTopic(LogData& data) { log_topic.Publish(data); }
