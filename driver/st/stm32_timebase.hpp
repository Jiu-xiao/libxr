#pragma once

#include "main.h"
#include "timebase.hpp"

namespace LibXR
{
/**
 * @brief STM32 SysTick 时间基准实现 / STM32 SysTick-based timebase implementation
 *
 * @note 必须使用标准 1 kHz HAL tick 语义：持续运行且未被重配的同一个
 * SysTick 每 1 ms 使 `HAL_GetTick()` 的数值恰好加 1。线程上下文以及不能抢占
 * SysTick 的中断可以安全读取；能够抢占 SysTick 的中断无法保证精确时间戳。
 * 连续中断屏蔽时间必须短于 1 ms。禁止从 SysTick handler、NMI 或 fault handler
 * 调用时间基。 / The standard 1 kHz HAL tick semantics are required: this
 * continuously running, unmodified SysTick must advance the numeric value returned by
 * `HAL_GetTick()` by exactly one every 1 ms. Reads are safe from threads and interrupts
 * that cannot preempt SysTick; exact timestamps are not guaranteed from interrupts that
 * can preempt it. Consecutive interrupt masking must remain shorter than 1 ms, and the
 * timebase must not be called from its handler, NMI, or fault handlers.
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
  [[deprecated(
      "STM32 SysTick timebase cannot guarantee exact timestamps in interrupts that "
      "preempt SysTick; use STM32TimerTimebase when ISR timestamps are required")]]
  STM32Timebase();
};

#ifdef HAL_TIM_MODULE_ENABLED

/**
 * @brief STM32 硬件定时器时间基准实现 / STM32 timer-based timebase implementation
 *
 * @note 必须使用标准 1 kHz HAL tick 语义：`timer` 是持续运行且未被重配的
 * 1 ms 向上计数器，每次更新使 `HAL_GetTick()` 的数值恰好加 1。其更新中断
 * 必须是唯一最高抢占优先级的可屏蔽中断，连续中断屏蔽时间必须短于 1 ms。该优先级由
 * CubeMX/BSP 配置；LibXR 不从句柄推导 IRQ，也不修改 NVIC。共享该 IRQ 的 handler
 * 必须保持短小且不能调用 RTOS API。禁止从该 timer handler、NMI 或 fault handler
 * 调用时间基。 / The standard 1 kHz HAL tick semantics are required: `timer` must be
 * a continuously running, unmodified 1 ms up-counter whose every update advances the
 * numeric value returned by `HAL_GetTick()` by exactly one. Its update interrupt must
 * be the unique highest-priority maskable interrupt, configured by CubeMX or the BSP;
 * LibXR neither derives an IRQ from the handle nor changes the NVIC. Handlers sharing
 * that IRQ must remain short and must not call RTOS APIs. Consecutive interrupt masking
 * must remain shorter than 1 ms, and the timebase must not be called from its handler,
 * NMI, or fault handlers.
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
