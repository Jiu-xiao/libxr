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
   */
  STM32Timebase();

  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   *
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds();

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   *
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds();
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
   */
  STM32TimerTimebase(TIM_HandleTypeDef* timer);

  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   *
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds();

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   *
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds();

  /**
   * @brief 硬件定时器句柄静态指针 / Static pointer to timer handle
   */
  static TIM_HandleTypeDef* htim;  // NOLINT
};

#endif

}  // namespace LibXR
