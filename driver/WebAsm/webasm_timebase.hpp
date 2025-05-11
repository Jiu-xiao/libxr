#pragma once

#include <chrono>
#include <cstdint>

#include "timebase.hpp"

namespace LibXR
{

/**
 * @brief WebAsmTimebase 类，用于获取 WebAssembly 系统的时间基准。
 */
class WebAsmTimebase : public Timebase
{
 public:
  WebAsmTimebase()
  {
    // 初始化基准时间点
    start_time_ = std::chrono::system_clock::now();
  }

  /**
   * @brief 获取当前时间戳（微秒级）
   * @return TimestampUS
   */
  TimestampUS _get_microseconds() override
  {
    auto now = std::chrono::system_clock::now();
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();
    return static_cast<TimestampUS>(us % UINT32_MAX);
  }

  /**
   * @brief 获取当前时间戳（毫秒级）
   * @return TimestampMS
   */
  TimestampMS _get_milliseconds() override
  {
    auto now = std::chrono::system_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    return static_cast<TimestampMS>(ms % UINT32_MAX);
  }

 private:
  std::chrono::system_clock::time_point start_time_;
};

}  // namespace LibXR
