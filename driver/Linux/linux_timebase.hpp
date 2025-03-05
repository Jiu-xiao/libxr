#include <sys/time.h>

#include "timebase.hpp"

extern struct timeval libxr_linux_start_time;

namespace LibXR {
class LinuxTimebase : public Timebase {
 public:
  TimestampUS _get_microseconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec)) %
           UINT32_MAX;
  }

  TimestampMS _get_milliseconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - libxr_linux_start_time.tv_sec) * 1000 +
            (tv.tv_usec - libxr_linux_start_time.tv_usec) / 1000) %
           UINT32_MAX;
  }
};
}  // namespace LibXR