#include "logger.hpp"

#include <cstdint>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message/message.hpp"

using namespace LibXR;

static Topic log_topic;  ///< 日志发布主题 / Log publish topic

void Logger::Init()
{
  log_topic = Topic::CreateTopic<LogData>("/xr/log", nullptr, true, false, false);

  void (*log_cb_fun)(bool in_isr, Topic, RawData& log_data) =
      [](bool, Topic tp, LibXR::RawData& log_data)
  {
    UNUSED(tp);

    auto log = reinterpret_cast<LogData*>(log_data.addr_);

    if (LIBXR_LOG_OUTPUT_LEVEL >= static_cast<uint8_t>(log->level) && STDIO::write_ &&
        STDIO::write_->Writable())
    {
      PrintToTerminal(*log);
    }
  };

  auto log_cb = LibXR::Topic::Callback::Create(log_cb_fun, log_topic);
  log_topic.RegisterCallback(log_cb);

  initialized_ = true;
}

MillisecondTimestamp Logger::Now() { return MillisecondTimestamp(Thread::GetTime()); }

void Logger::PublishRecord(LogData& data) { log_topic.Publish(data); }

void Logger::PrintToTerminal(const LogData& data)
{
  const char* color = GetColor(data.level);
  STDIO::Print<"{}{} [{}]({}:{}) {}{}\r\n">(
      color, LevelToString(data.level), static_cast<uint32_t>(data.timestamp), data.file,
      data.line, data.message, LIBXR_STYLE_STR[static_cast<uint8_t>(Style::RESET)]);
}

const char* Logger::GetColor(LogLevel level)
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

const char* Logger::LevelToString(LogLevel level)
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
