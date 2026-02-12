#include <sys/time.h>

#include "timebase.hpp"

extern struct timeval libxr_linux_start_time;

namespace LibXR
{
/**
 * @brief Linux 时间基准实现 / Linux timebase implementation
 *
 */
class LinuxTimebase : public Timebase
{
 public:
  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   *
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds()
  {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec)) %
           UINT32_MAX;
  }

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   *
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds()
  {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec) / 1000) %
           UINT32_MAX;
  }
};
}  // namespace LibXR
