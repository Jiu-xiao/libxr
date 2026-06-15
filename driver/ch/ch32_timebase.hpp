#pragma once

#include "ch32_def.hpp"
#include "libxr.hpp"

extern uint32_t SystemCoreClock;

namespace LibXR
{

/**
 * @brief CH32 时间基准实现 / CH32 timebase implementation
 */
class CH32Timebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * 配置 CH32 SysTick 时间基的回绕范围，并标记时间基已就绪。
   * Configures the CH32 SysTick wrap range and marks the timebase ready.
   */
  CH32Timebase();

  /**
   * @brief SysTick 中断入口辅助函数。
   *        Helper called from the SysTick interrupt path.
   */
  static inline void OnSysTickInterrupt();

  /**
   * @brief 同步毫秒计数器。
   *        Synchronize the millisecond counter.
   * @param ticks 新的毫秒计数值。New millisecond tick value.
   */
  void Sync(uint32_t ticks);

  /**
   * @brief SysTick 毫秒计数器。
   *        SysTick millisecond counter.
   */
  static inline volatile uint32_t sys_tick_ms_ = 0;
};

}  // namespace LibXR
