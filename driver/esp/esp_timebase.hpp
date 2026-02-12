#pragma once

#include "esp_timer.h"
#include "timebase.hpp"

namespace LibXR
{

/**
 * @brief ESP32 时间基准实现 / ESP32 timebase implementation
 *
 * 基于 `esp_timer` 提供微秒和毫秒时间戳。
 * Provides microsecond and millisecond timestamps based on `esp_timer`.
 */
class ESP32Timebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   */
  ESP32Timebase()
      : Timebase(static_cast<uint64_t>(UINT64_MAX), UINT32_MAX)
  {
  }

  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds() override
  {
    return esp_timer_get_time();
  }

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds() override
  {
    return static_cast<MillisecondTimestamp>(esp_timer_get_time() / 1000) % UINT32_MAX;
  }
};

}  // namespace LibXR
