#pragma once

#include "main.h"
#include "timebase.hpp"

namespace LibXR
{
/**
 * @brief STM32 SysTick 时间基准实现 / STM32 SysTick-based timebase implementation
 *
 * @note SysTick 必须持续按 1 ms 周期运行，每次中断使 HAL tick 加 1，使用期间不可重配。
 *       读取方不能抢占 SysTick 中断，也不能在 SysTick、NMI 或 fault handler 中读取。
 *       中断处理延迟必须短于 1 ms；时钟和优先级由 BSP 配置。
 *       SysTick must run continuously with a 1 ms period and advance HAL tick by one
 *       per interrupt, without reconfiguration. Readers must not preempt SysTick or
 *       run in its handler, NMI, or fault handlers. Interrupt service latency must stay
 *       below 1 ms; the BSP configures the clock and priorities.
 * @note 微秒读取补偿尚未处理的回绕；毫秒读取仍直接返回 HAL tick，可能暂时落后。
 *       Microsecond reads compensate a pending rollover; millisecond reads return the
 *       raw HAL tick and may temporarily lag.
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
 *
 * @note timer 必须是 HAL tick 的 1 ms 向上计数源，持续运行，每次更新使 HAL tick 加 1。
 *       读取方不能抢占该更新中断，也不能在该 handler、NMI 或 fault handler 中读取。
 *       更新中断处理延迟必须短于 1 ms；计数器配置和优先级由 BSP 保持不变。
 *       The timer must be the continuously running 1 ms HAL tick up-counter, advancing
 *       HAL tick by one per update. Readers must not preempt its update interrupt or
 *       run in that handler, NMI, or fault handlers. Update service latency must stay
 *       below 1 ms; the BSP keeps the counter configuration and priorities unchanged.
 * @note 微秒读取补偿尚未处理的回绕；毫秒读取仍直接返回 HAL tick，可能暂时落后。
 *       Microsecond reads compensate a pending rollover; millisecond reads return the
 *       raw HAL tick and may temporarily lag.
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
