#include <time.h>

#include "timebase.hpp"

extern struct timespec libxr_linux_start_time_spec;

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
   * @brief 构造函数 / Constructor
   *
   * 记录 Linux monotonic 参考起点并标记时间基已就绪。
   * Captures the Linux monotonic reference point and marks the timebase ready.
   */
  LinuxTimebase();
};
}  // namespace LibXR
