#pragma once

#include "main.h"
#include "timebase.hpp"

namespace LibXR
{
/**
 * @brief STM32 SysTick 时间基准实现 / STM32 SysTick-based timebase implementation
 */
class STM32Timebase : public Timebase
{
 public:
  /**
   * @brief 默认构造函数 / Default constructor
   *
   * 选择 SysTick 作为当前时间基后端，并配置对应的回绕范围。
   * Selects SysTick as the active backend and configures the matching wrap range.
   */
  STM32Timebase();
};

#ifdef HAL_TIM_MODULE_ENABLED

/**
 * @brief STM32 硬件定时器时间基准实现 / STM32 timer-based timebase implementation
 */
class STM32TimerTimebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param timer 定时器句柄指针 / Pointer to timer handle
   *
   * 选择硬件定时器作为当前时间基后端，并缓存句柄供静态入口使用。
   * Selects the hardware timer as the active backend and caches the handle for
   * the static entry points.
   */
  STM32TimerTimebase(TIM_HandleTypeDef* timer);

  /**
   * @brief 硬件定时器句柄静态指针 / Static pointer to timer handle
   */
  static TIM_HandleTypeDef* htim;  // NOLINT
};

#endif

}  // namespace LibXR
