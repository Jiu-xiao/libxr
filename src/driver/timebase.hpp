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
  Timebase() { timebase = this; }

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
  static TimestampUS GetMicroseconds() { return timebase->_get_microseconds(); }

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
  static TimestampMS GetMilliseconds() { return timebase->_get_milliseconds(); }

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
  virtual TimestampUS _get_microseconds() = 0;  // NOLINT

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
  virtual TimestampMS _get_milliseconds() = 0;  // NOLINT

  /**
   * @brief 静态指针，用于存储全局时间基对象。
   *        Static pointer storing the global timebase instance.
   *
   * 该指针指向当前生效的 `Timebase` 实例，所有静态时间函数都通过该指针访问实际实现。
   * This pointer refers to the currently active `Timebase` instance,
   * and all static time functions access the actual implementation through this pointer.
   */
  static Timebase *timebase;  // NOLINT
};

}  // namespace LibXR
