#include "logger.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"

using namespace LibXR;

static Topic log_topic;  ///< 日志发布主题 / Log publish topic

void Logger::Init()
{
  log_topic = Topic::CreateTopic<LogData>("/xr/log", nullptr, true, false, true);

#if LIBXR_PRINTF_BUFFER_SIZE > 0
  void (*log_cb_fun)(bool in_isr, Topic, RawData &log_data) =
      [](bool, Topic tp, LibXR::RawData &log_data)
  {
    UNUSED(tp);

    auto log = reinterpret_cast<LogData *>(log_data.addr_);

    if (LIBXR_LOG_OUTPUT_LEVEL >= static_cast<uint8_t>(log->level) && STDIO::write_ &&
        STDIO::write_->Writable())
    {
      PrintToTerminal(*log);
    }
  };

  auto log_cb = LibXR::Topic::Callback::Create(log_cb_fun, log_topic);
  log_topic.RegisterCallback(log_cb);
#endif

  initialized_ = true;
}

// NOLINTNEXTLINE
void Logger::Publish(LogLevel level, const char *file, uint32_t line, const char *fmt,
                     ...)
{
  if (!initialized_)
  {
    Init();
  }
  LogData data;
  data.timestamp = MillisecondTimestamp(Thread::GetTime());
  data.level = level;
  data.file = file;
  data.line = line;

  va_list args;
  va_start(args, fmt);
  auto ans = vsnprintf(data.message, sizeof(data.message), fmt, args);
  UNUSED(ans);
  va_end(args);

  log_topic.Publish(data);
}

void Logger::PrintToTerminal(const LogData &data)
{
  const char *color = GetColor(data.level);

  STDIO::Printf("%s%s [%u](%s:%u) %s%s\r\n", color, LevelToString(data.level),
                static_cast<uint32_t>(data.timestamp), data.file, data.line, data.message,
                LIBXR_FORMAT_STR[static_cast<uint8_t>(Format::RESET)]);
}

const char *Logger::GetColor(LogLevel level)
{
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

const char *Logger::LevelToString(LogLevel level)
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
