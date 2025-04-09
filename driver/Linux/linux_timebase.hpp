#include <sys/time.h>

#include "timebase.hpp"

extern struct timeval libxr_linux_start_time;

namespace LibXR
{
/**
 * @brief LinuxTimebase 类，用于获取 Linux 系统的时间基准。Provides a timebase for Linux
 * systems.
 *
 */
class LinuxTimebase : public Timebase
{
 public:
  /**
   * @brief 获取当前时间戳（微秒级）。Returns the current timestamp in microseconds.
   *
   * @return TimestampUS
   */
  TimestampUS _get_microseconds()
  {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec)) %
           UINT32_MAX;
  }

  /**
   * @brief 获取当前时间戳（毫秒级）
   *
   * @return TimestampMS
   */
  TimestampMS _get_milliseconds()
  {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec) / 1000) %
           UINT32_MAX;
  }
};
}  // namespace LibXR