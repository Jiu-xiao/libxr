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
  log_topic = Topic::CreateTopic<LogData>("/xr/log", nullptr, false, true);

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
