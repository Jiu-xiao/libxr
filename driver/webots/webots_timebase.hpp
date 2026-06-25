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
   * @brief 构造函数 / Constructor
   *
   * 标记 Webots 时间基已就绪；具体计数由仿真时钟提供。
   * Marks the Webots timebase ready; the actual counter is driven by the
   * simulator clock.
   */
  WebotsTimebase();
};
}  // namespace LibXR
