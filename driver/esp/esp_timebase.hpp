#pragma once

#include "esp_timer.h"
#include "timebase.hpp"

namespace LibXR
{

/**
 * @class ESP32Timebase
 * @brief 基于 esp_timer 的时间基准实现（微秒与毫秒）
 *        Timebase implementation using esp_timer (microseconds and milliseconds)
 */
class ESP32Timebase : public Timebase
{
 public:
  /**
   * @brief 构造函数
   */
  ESP32Timebase()
      : Timebase(static_cast<uint64_t>(UINT64_MAX), UINT32_MAX)  // 最大值可根据需要修改
  {
  }

  /**
   * @brief 获取当前时间（微秒）
   * @return MicrosecondTimestamp 当前时间（微秒）
   */
  MicrosecondTimestamp _get_microseconds() override
  {
    return esp_timer_get_time();  // 返回自启动以来的微秒数
  }

  /**
   * @brief 获取当前时间（毫秒）
   * @return MillisecondTimestamp 当前时间（毫秒）
   */
  MillisecondTimestamp _get_milliseconds() override
  {
    return static_cast<MillisecondTimestamp>(esp_timer_get_time() / 1000) % UINT32_MAX;
  }
};

}  // namespace LibXR
