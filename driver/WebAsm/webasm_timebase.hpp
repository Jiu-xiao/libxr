#pragma once

#include <chrono>
#include <cstdint>

#include "timebase.hpp"

namespace LibXR
{

/**
 * @brief WebAssembly 时间基准实现 / WebAssembly timebase implementation
 */
class WebAsmTimebase : public Timebase
{
 public:
  WebAsmTimebase()
  {
    // Initialize reference timestamp.
    start_time_ = std::chrono::system_clock::now();
  }

  /**
   * @brief 获取当前微秒计数 / Get current timestamp in microseconds
   * @return MicrosecondTimestamp 微秒时间戳 / Microsecond timestamp
   */
  MicrosecondTimestamp _get_microseconds() override
  {
    auto now = std::chrono::system_clock::now();
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_).count();
    return static_cast<MicrosecondTimestamp>(us % UINT32_MAX);
  }

  /**
   * @brief 获取当前毫秒计数 / Get current timestamp in milliseconds
   * @return MillisecondTimestamp 毫秒时间戳 / Millisecond timestamp
   */
  MillisecondTimestamp _get_milliseconds() override
  {
    auto now = std::chrono::system_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    return static_cast<MillisecondTimestamp>(ms % UINT32_MAX);
  }

 private:
  std::chrono::system_clock::time_point start_time_;
};

}  // namespace LibXR
