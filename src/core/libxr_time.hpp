#pragma once

#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

extern uint64_t libxr_timebase_max_valid_us;  // NOLINT
extern uint32_t libxr_timebase_max_valid_ms;  // NOLINT

namespace LibXR
{

/**
 * @class MicrosecondTimestamp
 * @brief 表示微秒级时间戳的类。Class representing a timestamp in microseconds.
 */
class MicrosecondTimestamp
{
 public:
  /**
   * @brief 默认构造函数，初始化时间戳为 0。
   * Default constructor initializing the timestamp to 0.
   */
  MicrosecondTimestamp();

  /**
   * @brief 以给定的微秒值构造时间戳。
   * Constructor initializing the timestamp with a given microsecond value.
   * @param microsecond 以微秒表示的时间值。Time value in microseconds.
   */
  MicrosecondTimestamp(uint64_t microsecond);

  /**
   * @brief 转换运算符，将时间戳转换为 uint64_t。
   * Conversion operator to uint64_t.
   */
  operator uint64_t() const;

  /**
   * @class Duration
   * @brief 表示微秒级时间差的类。Class representing a time difference in microseconds.
   */
  class Duration
  {
   public:
    /**
     * @brief 构造函数，初始化时间差。
     * Constructor initializing the time difference.
     * @param diff 以微秒表示的时间差。Time difference in microseconds.
     */
    Duration(uint64_t diff);

    /**
     * @brief 转换运算符，将时间差转换为 uint64_t。
     * Conversion operator to uint64_t.
     */
    operator uint64_t() const;

    /**
     * @brief 以秒返回时间差（double 类型）。
     * Returns the time difference in seconds as a double.
     */
    [[nodiscard]] double ToSecond() const;

    /**
     * @brief 以秒返回时间差（float 类型）。
     * Returns the time difference in seconds as a float.
     */
    [[nodiscard]] float ToSecondf() const;

    /**
     * @brief 以微秒返回时间差。
     * Returns the time difference in microseconds.
     */
    [[nodiscard]] uint64_t ToMicrosecond() const;

    /**
     * @brief 以毫秒返回时间差。
     * Returns the time difference in milliseconds.
     */
    [[nodiscard]] uint32_t ToMillisecond() const;

   private:
    uint64_t diff_;  ///< 存储时间差（微秒）。Time difference stored in microseconds.
  };

  /**
   * @brief 计算两个时间戳之间的时间差。
   * Computes the time difference between two timestamps.
   * @param old_microsecond 旧的时间戳。The older timestamp.
   * @return Duration 计算得到的时间差。Computed time difference.
   */
  Duration operator-(const MicrosecondTimestamp& old_microsecond) const;

  /**
   * @brief 赋值运算符重载。
   * Assignment operator overload.
   * @param other 另一个 MicrosecondTimestamp 对象。Another MicrosecondTimestamp object.
   * @return 返回当前对象的引用。Returns a reference to the current object.
   */
  MicrosecondTimestamp& operator=(const MicrosecondTimestamp& other);

 private:
  uint64_t microsecond_;  ///< 以微秒存储的时间戳。Timestamp stored in microseconds.
};

/**
 * @class MillisecondTimestamp
 * @brief 表示毫秒级时间戳的类。Class representing a timestamp in milliseconds.
 */
class MillisecondTimestamp
{
 public:
  MillisecondTimestamp();
  MillisecondTimestamp(uint32_t millisecond);
  operator uint32_t() const;

  /**
   * @class Duration
   * @brief 表示毫秒级时间差的类。Class representing a time difference in milliseconds.
   */
  class Duration
  {
   public:
    /**
     * @brief 构造函数，初始化时间差。
     * Constructor initializing the time difference.
     * @param diff 以毫秒表示的时间差。Time difference in milliseconds.
     */
    Duration(uint32_t diff);

    /**
     * @brief 转换运算符，将时间差转换为 uint32_t。
     * Conversion operator to uint32_t.
     */
    operator uint32_t() const;

    /**
     * @brief 以秒返回时间差（double 类型）。
     * Returns the time difference in seconds as a double.
     */
    [[nodiscard]] double ToSecond() const;

    /**
     * @brief 以秒返回时间差（float 类型）。
     * Returns the time difference in seconds as a float.
     */
    [[nodiscard]] float ToSecondf() const;

    /**
     * @brief 以毫秒返回时间差。
     * Returns the time difference in milliseconds.
     */
    [[nodiscard]] uint32_t ToMillisecond() const;

    /**
     * @brief 以微秒返回时间差。
     * Returns the time difference in microseconds.
     */
    [[nodiscard]] uint64_t ToMicrosecond() const;

   private:
    uint32_t diff_;  ///< 存储时间差（毫秒）。Time difference stored in milliseconds.
  };

  /**
   * @brief 计算两个时间戳之间的时间差。
   * Computes the time difference between two timestamps.
   * @param old_millisecond 旧的时间戳。The older timestamp.
   * @return Duration 计算得到的时间差。Computed time difference.
   */
  [[nodiscard]] Duration operator-(const MillisecondTimestamp& old_millisecond) const;

 private:
  uint32_t millisecond_;  ///< 以毫秒存储的时间戳。Timestamp stored in milliseconds.
};

}  // namespace LibXR
