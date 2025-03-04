#include "timebase.hpp"
#include <sys/time.h>

extern struct timeval _libxr_linux_start_time;

namespace LibXR {
class LinuxTimebase : public Timebase {
public:
  TimestampUS _get_microseconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - _libxr_linux_start_time.tv_sec) * 1000000 +
            (tv.tv_usec - _libxr_linux_start_time.tv_usec)) %
           UINT32_MAX;
  }

  TimestampMS _get_milliseconds() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return ((tv.tv_sec - _libxr_linux_start_time.tv_sec) * 1000 +
            (tv.tv_usec - _libxr_linux_start_time.tv_usec) / 1000) %
           UINT32_MAX;
  }
};
} // namespace LibXR