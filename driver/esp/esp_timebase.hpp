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
   *
   * 初始化 ESP 时间基，并在支持的芯片上启用 systimer 快速路径。
   * Initializes the ESP timebase and enables the systimer fast path when the
   * target supports it.
   */
  ESP32Timebase();
};

}  // namespace LibXR
