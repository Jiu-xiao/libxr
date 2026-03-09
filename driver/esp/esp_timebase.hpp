#pragma once

#include "esp_def.hpp"

#include "esp_timer.h"
#include "soc/soc_caps.h"
#if SOC_SYSTIMER_SUPPORTED
#include "hal/systimer_hal.h"
#endif
#include "timebase.hpp"

namespace LibXR
{

/**
 * @brief ESP32 时间基准实现 / ESP32 timebase implementation
 *
 * 基于 `esp_timer` 提供微秒和毫秒时间戳。
 * 在支持的平台上使用 `systimer HAL` 快速路径。
 * Provides microsecond and millisecond timestamps based on `esp_timer`.
 * Uses optional systimer HAL fast path when available.
 */
class ESP32Timebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   */
  ESP32Timebase();

  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds() override;

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds() override;

 private:
#if SOC_SYSTIMER_SUPPORTED
  systimer_hal_context_t systimer_hal_ = {};  ///< Systimer HAL context
  bool systimer_ready_ = false;               ///< Systimer fast path status
#endif
};

}  // namespace LibXR
