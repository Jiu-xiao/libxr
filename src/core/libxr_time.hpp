#pragma once

#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

namespace LibXR
{

/**
 * @class TimestampUS
 * @brief 表示微秒级时间戳的类。Class representing a timestamp in microseconds.
 */
class TimestampUS
{
 public:
  /**
   * @brief 默认构造函数，初始化时间戳为 0。
   * Default constructor initializing the timestamp to 0.
   */
  TimestampUS() : microsecond_(0) {}

  /**
   * @brief 以给定的微秒值构造时间戳。
   * Constructor initializing the timestamp with a given microsecond value.
   * @param microsecond 以微秒表示的时间值。Time value in microseconds.
   */
  TimestampUS(uint64_t microsecond) : microsecond_(microsecond) {}

  /**
   * @brief 转换运算符，将时间戳转换为 uint64_t。
   * Conversion operator to uint64_t.
   */
  operator uint64_t() const { return microsecond_; }

  /**
   * @class TimeDiffUS
   * @brief 表示微秒级时间差的类。Class representing a time difference in microseconds.
   */
  class TimeDiffUS
  {
   public:
    /**
     * @brief 构造函数，初始化时间差。
     * Constructor initializing the time difference.
     * @param diff 以微秒表示的时间差。Time difference in microseconds.
     */
    TimeDiffUS(uint64_t diff) : diff_(diff) {}

    /**
     * @brief 转换运算符，将时间差转换为 uint64_t。
     * Conversion operator to uint64_t.
     */
    operator uint64_t() const { return diff_; }

    /**
     * @brief 以秒返回时间差（double 类型）。
     * Returns the time difference in seconds as a double.
     */
    double to_second() const { return static_cast<double>(diff_) / 1000000.0; }

    /**
     * @brief 以秒返回时间差（float 类型）。
     * Returns the time difference in seconds as a float.
     */
    float to_secondf() const { return static_cast<float>(diff_) / 1000000.0f; }

    /**
     * @brief 以微秒返回时间差。
     * Returns the time difference in microseconds.
     */
    uint64_t to_microsecond() const { return diff_; }

    /**
     * @brief 以毫秒返回时间差。
     * Returns the time difference in milliseconds.
     */
    uint64_t to_millisecond() const { return diff_ / 1000u; }

   private:
    uint64_t diff_;  ///< 存储时间差（微秒）。Time difference stored in microseconds.
  };

  /**
   * @brief 计算两个时间戳之间的时间差。
   * Computes the time difference between two timestamps.
   * @param old_microsecond 旧的时间戳。The older timestamp.
   * @return TimeDiffUS 计算得到的时间差。Computed time difference.
   */
  TimeDiffUS operator-(const TimestampUS &old_microsecond) const
  {
    ASSERT(microsecond_ >= old_microsecond.microsecond_);
    return TimeDiffUS(microsecond_ - old_microsecond.microsecond_);
  }

  /**
   * @brief 赋值运算符重载。
   * Assignment operator overload.
   * @param other 另一个 TimestampUS 对象。Another TimestampUS object.
   * @return 返回当前对象的引用。Returns a reference to the current object.
   */
  TimestampUS &operator=(const TimestampUS &other)
  {
    if (this != &other)
    {
      microsecond_ = other.microsecond_;
    }
    return *this;
  }

 private:
  uint64_t microsecond_;  ///< 以微秒存储的时间戳。Timestamp stored in microseconds.
};

/**
 * @class TimestampMS
 * @brief 表示毫秒级时间戳的类。Class representing a timestamp in milliseconds.
 */
class TimestampMS
{
 public:
  TimestampMS() : millisecond_(0) {}
  TimestampMS(uint32_t millisecond) : millisecond_(millisecond) {}
  operator uint32_t() const { return millisecond_; }

  /**
   * @class TimeDiffMS
   * @brief 表示毫秒级时间差的类。Class representing a time difference in milliseconds.
   */
  class TimeDiffMS
  {
   public:
    /**
     * @brief 构造函数，初始化时间差。
     * Constructor initializing the time difference.
     * @param diff 以毫秒表示的时间差。Time difference in milliseconds.
     */
    TimeDiffMS(uint32_t diff) : diff_(diff) {}

    /**
     * @brief 转换运算符，将时间差转换为 uint32_t。
     * Conversion operator to uint32_t.
     */
    operator uint32_t() const { return diff_; }

    /**
     * @brief 以秒返回时间差（double 类型）。
     * Returns the time difference in seconds as a double.
     */
    double to_second() { return static_cast<double>(diff_) / 1000.0; }

    /**
     * @brief 以秒返回时间差（float 类型）。
     * Returns the time difference in seconds as a float.
     */
    float to_secondf() { return static_cast<float>(diff_) / 1000.0f; }

    /**
     * @brief 以毫秒返回时间差。
     * Returns the time difference in milliseconds.
     */
    uint32_t to_millisecond() const { return diff_; }

    /**
     * @brief 以微秒返回时间差。
     * Returns the time difference in microseconds.
     */
    uint32_t to_microsecond() const { return diff_ * 1000u; }

   private:
    uint32_t diff_;  ///< 存储时间差（毫秒）。Time difference stored in milliseconds.
  };

  /**
   * @brief 计算两个时间戳之间的时间差。
   * Computes the time difference between two timestamps.
   * @param old_millisecond 旧的时间戳。The older timestamp.
   * @return TimeDiffMS 计算得到的时间差。Computed time difference.
   */
  TimeDiffMS operator-(TimestampMS &old_millisecond)
  {
    ASSERT(millisecond_ >= old_millisecond);
    return TimeDiffMS(millisecond_ - old_millisecond);
  }

 private:
  uint32_t millisecond_;  ///< 以毫秒存储的时间戳。Timestamp stored in milliseconds.
};

}  // namespace LibXR
