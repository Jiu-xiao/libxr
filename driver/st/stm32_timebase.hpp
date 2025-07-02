#pragma once

#include "main.h"
#include "timebase.hpp"

namespace LibXR
{
/**
 * @class STM32Timebase
 * @brief 获取基于STM32 SysTick计数器的时间基准（微秒与毫秒） / Provides a timebase using
 * STM32 SysTick timer (microseconds and milliseconds)
 */
class STM32Timebase : public Timebase
{
 public:
  /**
   * @brief 默认构造函数 / Default constructor
   */
  STM32Timebase();

  /**
   * @brief 获取当前时间（微秒级） / Get current time in microseconds
   *
   * 该函数通过读取SysTick计数器的值和系统滴答定时器的毫秒数，计算当前的微秒时间。
   * It calculates current microsecond timestamp based on SysTick counter and system tick
   * value.
   *
   * @return MicrosecondTimestamp 当前时间（微秒） / Current timestamp in microseconds
   */
  MicrosecondTimestamp _get_microseconds();

  /**
   * @brief 获取当前时间（毫秒级） / Get current time in milliseconds
   *
   * @return MillisecondTimestamp 当前时间（毫秒） / Current timestamp in milliseconds
   */
  MillisecondTimestamp _get_milliseconds();
};

#ifdef HAL_TIM_MODULE_ENABLED

/**
 * @class STM32TimerTimebase
 * @brief 基于硬件定时器的时间基准类 / Provides a timebase using hardware timer (TIM)
 */
class STM32TimerTimebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   * @param timer 定时器句柄指针 / Pointer to hardware timer handle
   */
  STM32TimerTimebase(TIM_HandleTypeDef* timer);

  /**
   * @brief 获取当前时间（微秒级） / Get current time in microseconds
   *
   * 该函数通过读取硬件定时器计数值和系统滴答定时器毫秒数来计算当前的微秒级时间戳。
   * It calculates current microsecond timestamp based on hardware timer counter and
   * system tick value.
   *
   * @return MicrosecondTimestamp 当前时间（微秒） / Current timestamp in microseconds
   */
  MicrosecondTimestamp _get_microseconds();

  /**
   * @brief 获取当前时间（毫秒级） / Get current time in milliseconds
   *
   * @return MillisecondTimestamp 当前时间（毫秒） / Current timestamp in milliseconds
   */
  MillisecondTimestamp _get_milliseconds();

  /**
   * @brief 硬件定时器句柄指针 / Static pointer to hardware timer handle
   */
  static TIM_HandleTypeDef* htim;  // NOLINT
};

#endif

}  // namespace LibXR
