#pragma once

#include "libxr_time.hpp"

namespace LibXR
{

/**
 * @brief 时间基类，用于提供高精度时间戳。
 *        Timebase class for providing high-precision timestamps.
 *
 * 该类提供了微秒和毫秒级的时间戳获取接口，并要求派生类实现具体的时间获取方法。
 * This class provides interfaces for obtaining timestamps in microseconds and
 * milliseconds, and requires derived classes to implement the actual time retrieval
 * methods.
 */
class Timebase
{
 public:
  /**
   * @brief 默认构造函数，初始化全局时间基指针。
   *        Default constructor, initializing the global timebase pointer.
   *
   * 该构造函数在实例化对象时，将 `timebase` 指针指向当前对象，
   * 以便静态方法可以访问具体的时间基实例。
   * This constructor sets the `timebase` pointer to the current object
   * when an instance is created, allowing static methods to access the specific timebase
   * instance.
   */
  Timebase(uint64_t max_valid_us = UINT64_MAX, uint32_t max_valid_ms = UINT32_MAX)
  {
    libxr_timebase_max_valid_ms = max_valid_ms;
    libxr_timebase_max_valid_us = max_valid_us;
    timebase = this;
  }

  /**
   * @brief 获取当前时间的微秒级时间戳。
   *        Gets the current timestamp in microseconds.
   *
   * 该函数通过静态方法调用 `_get_microseconds()`，
   * 从 `timebase` 实例中获取当前时间。
   * This function calls `_get_microseconds()` via a static method
   * to retrieve the current time from the `timebase` instance.
   *
   * @return 返回当前的时间戳（单位：微秒）。
   *         Returns the current timestamp (in microseconds).
   */
  static MicrosecondTimestamp GetMicroseconds() { return timebase->_get_microseconds(); }

  /**
   * @brief 获取当前时间的毫秒级时间戳。
   *        Gets the current timestamp in milliseconds.
   *
   * 该函数通过静态方法调用 `_get_milliseconds()`，
   * 从 `timebase` 实例中获取当前时间。
   * This function calls `_get_milliseconds()` via a static method
   * to retrieve the current time from the `timebase` instance.
   *
   * @return 返回当前的时间戳（单位：毫秒）。
   *         Returns the current timestamp (in milliseconds).
   */
  static MillisecondTimestamp GetMilliseconds() { return timebase->_get_milliseconds(); }

  /**
   * @brief 微秒级延时 / Delay in microseconds
   * @param us 延时长度（us）/ Delay length (us)
   */
  static inline void DelayMicroseconds(uint32_t us)
  {
    if (us == 0u)
    {
      return;
    }

    const uint64_t START = static_cast<uint64_t>(Timebase::GetMicroseconds());
    while ((static_cast<uint64_t>(Timebase::GetMicroseconds()) - START) < us)
    {
      // busy-wait
    }
  }

  /**
   * @brief 纯虚函数，获取当前时间的微秒级时间戳（由派生类实现）。
   *        Pure virtual function for obtaining the current timestamp in microseconds
   * (implemented by derived classes).
   *
   * 该函数需要在派生类中实现，以提供具体的时间获取机制。
   * This function must be implemented in derived classes to provide
   * the actual time retrieval mechanism.
   *
   * @return 返回当前的时间戳（单位：微秒）。
   *         Returns the current timestamp (in microseconds).
   */
  virtual MicrosecondTimestamp _get_microseconds() = 0;  // NOLINT

  /**
   * @brief 纯虚函数，获取当前时间的毫秒级时间戳（由派生类实现）。
   *        Pure virtual function for obtaining the current timestamp in milliseconds
   * (implemented by derived classes).
   *
   * 该函数需要在派生类中实现，以提供具体的时间获取机制。
   * This function must be implemented in derived classes to provide
   * the actual time retrieval mechanism.
   *
   * @return 返回当前的时间戳（单位：毫秒）。
   *         Returns the current timestamp (in milliseconds).
   */
  virtual MillisecondTimestamp _get_milliseconds() = 0;  // NOLINT

  /**
   * @brief 静态指针，用于存储全局时间基对象。
   *        Static pointer storing the global timebase instance.
   *
   * 该指针指向当前生效的 `Timebase` 实例，所有静态时间函数都通过该指针访问实际实现。
   * This pointer refers to the currently active `Timebase` instance,
   * and all static time functions access the actual implementation through this pointer.
   */
  static inline Timebase *timebase = nullptr;  // NOLINT
};

}  // namespace LibXR
