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
  STM32Timebase() {}

  /**
   * @brief 获取当前时间（微秒级） / Get current time in microseconds
   *
   * 该函数通过读取SysTick计数器的值和系统滴答定时器的毫秒数，计算当前的微秒时间。
   * It calculates current microsecond timestamp based on SysTick counter and system tick
   * value.
   *
   * @return TimestampUS 当前时间（微秒） / Current timestamp in microseconds
   */
  TimestampUS _get_microseconds()
  {
    uint32_t ms_old = HAL_GetTick();
    uint32_t tick_value_old = SysTick->VAL;
    uint32_t ms_new = HAL_GetTick();
    uint32_t tick_value_new = SysTick->VAL;

    auto time_diff = ms_new - ms_old;
    switch (time_diff)
    {
      case 0:
        return ms_new * 1000 + 1000 - tick_value_old * 1000 / (SysTick->LOAD + 1);
      case 1:
        /* 中断发生在两次读取之间 / Interrupt happened between two reads */
        return ms_new * 1000 + 1000 - tick_value_new * 1000 / (SysTick->LOAD + 1);
      default:
        /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
         * 1ms, an error case */
        ASSERT(false);
    }

    return 0;
  }

  /**
   * @brief 获取当前时间（毫秒级） / Get current time in milliseconds
   *
   * @return TimestampMS 当前时间（毫秒） / Current timestamp in milliseconds
   */
  TimestampMS _get_milliseconds() { return HAL_GetTick(); }
};

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
  STM32TimerTimebase(TIM_HandleTypeDef* timer) { htim = timer; }

  /**
   * @brief 获取当前时间（微秒级） / Get current time in microseconds
   *
   * 该函数通过读取硬件定时器计数值和系统滴答定时器毫秒数来计算当前的微秒级时间戳。
   * It calculates current microsecond timestamp based on hardware timer counter and
   * system tick value.
   *
   * @return TimestampUS 当前时间（微秒） / Current timestamp in microseconds
   */
  TimestampUS _get_microseconds()
  {
    uint32_t ms_old = HAL_GetTick();
    uint32_t tick_value_old = __HAL_TIM_GET_COUNTER(htim);
    uint32_t ms_new = HAL_GetTick();
    uint32_t tick_value_new = __HAL_TIM_GET_COUNTER(htim);

    auto time_diff = ms_new - ms_old;
    switch (time_diff)
    {
      case 0:
        return ms_new * 1000 + 1000 -
               tick_value_old * 1000 / (__HAL_TIM_GET_AUTORELOAD(htim) + 1);
      case 1:
        /* 中断发生在两次读取之间 / Interrupt happened between two reads */
        return ms_new * 1000 + 1000 -
               tick_value_new * 1000 / (__HAL_TIM_GET_AUTORELOAD(htim) + 1);
      default:
        /* 中断耗时过长（超过1ms），程序异常 / Indicates that interrupt took more than
         * 1ms, an error case */
        ASSERT(false);
    }

    return 0;
  }

  /**
   * @brief 获取当前时间（毫秒级） / Get current time in milliseconds
   *
   * @return TimestampMS 当前时间（毫秒） / Current timestamp in milliseconds
   */
  TimestampMS _get_milliseconds() { return HAL_GetTick(); }

  /**
   * @brief 硬件定时器句柄指针 / Static pointer to hardware timer handle
   */
  static TIM_HandleTypeDef* htim;
};

}  // namespace LibXR
