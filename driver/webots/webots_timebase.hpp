#pragma once

#include "libxr_system.hpp"
#include "timebase.hpp"

extern struct timeval libxr_Webots_start_time;

namespace LibXR
{
/**
 * @brief Webots 时间基准实现 / Webots timebase implementation
 *
 */
class WebotsTimebase : public Timebase
{
 public:
  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   *
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds() { return _libxr_webots_time_count * 1000; }

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   *
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds() { return _libxr_webots_time_count; }
};
}  // namespace LibXR
