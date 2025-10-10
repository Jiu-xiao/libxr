#include "libxr_system.hpp"
#include "timebase.hpp"

extern struct timeval libxr_Webots_start_time;

namespace LibXR
{
/**
 * @brief WebotsTimebase 类，用于获取 Webots 的时间基准。Provides a timebase for Webots.
 *
 */
class WebotsTimebase : public Timebase
{
 public:
  /**
   * @brief 获取当前时间戳（微秒级）。Returns the current timestamp in microseconds.
   *
   * @return MicrosecondTimestamp
   */
  MicrosecondTimestamp _get_microseconds() { return _libxr_webots_time_count * 1000; }

  /**
   * @brief 获取当前时间戳（毫秒级）
   *
   * @return MillisecondTimestamp
   */
  MillisecondTimestamp _get_milliseconds() { return _libxr_webots_time_count; }
};
}  // namespace LibXR