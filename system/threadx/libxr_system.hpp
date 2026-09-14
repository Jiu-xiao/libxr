#pragma once

#include <stdint.h>

#include "tx_api.h"

namespace LibXR
{
typedef TX_MUTEX libxr_mutex_handle;
typedef TX_SEMAPHORE libxr_semaphore_handle;
typedef TX_THREAD* libxr_thread_handle;

namespace Detail
{
/**
 * @brief 将有限毫秒等待向上换算为 ThreadX tick / Round a finite millisecond wait up to
 * ticks.
 * @note 超出范围时取最大有限 tick，避免使用永久等待值。
 *       Saturate at the largest finite tick count, excluding the forever sentinel.
 */
[[nodiscard]] inline ULONG MillisecondsToThreadXTicks(uint32_t milliseconds)
{
  const uint64_t ticks =
      (static_cast<uint64_t>(milliseconds) * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U;
  return ticks >= static_cast<uint64_t>(TX_WAIT_FOREVER) ? TX_WAIT_FOREVER - 1U
                                                         : static_cast<ULONG>(ticks);
}
}  // namespace Detail

/**
 * @brief  平台初始化函数
 *         Platform initialization function
 * @param  timer_pri  定时器任务的优先级（默认值 2）
 *                    Timer task priority (default: 2)
 * @param  timer_stack_depth  定时器任务的栈深度（默认值 512）
 *                            Timer task stack depth (default: 512)
 *
 * @details
 * 该函数用于初始化 ThreadX 相关资源，如定时器任务。
 * 它设置定时器任务的优先级 `timer_pri` 和栈深度 `timer_stack_depth`。
 *
 * This function initializes ThreadX-related resources, such as the timer task.
 * It sets the timer task priority (`timer_pri`) and stack depth (`timer_stack_depth`).
 */
void PlatformInit(uint32_t timer_pri = 2,
                  uint32_t timer_stack_depth = 512);  // NOLINT
}  // namespace LibXR
