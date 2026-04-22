#pragma once

#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

/**
 * @brief 微秒时间基准的最大有效值 / Maximum valid value of the microsecond timebase
 *
 * @note 当微秒时间基准发生回绕时，`MicrosecondTimestamp::operator-` 使用该值恢复跨回绕的
 *       时间差。默认值为 `UINT64_MAX`，表示 64 位时间基准的完整计数范围。
 *       / When the microsecond timebase wraps around,
 *       `MicrosecondTimestamp::operator-` uses this value to recover the elapsed
 *       time across the wrap. The default `UINT64_MAX` means the full range of a
 *       64-bit timebase.
 */
inline uint64_t libxr_timebase_max_valid_us = UINT64_MAX;  // NOLINT

/**
 * @brief 毫秒时间基准的最大有效值 / Maximum valid value of the millisecond timebase
 *
 * @note 当毫秒时间基准发生回绕时，`MillisecondTimestamp::operator-` 使用该值恢复跨回绕的
 *       时间差。默认值为 `UINT32_MAX`，表示 32 位时间基准的完整计数范围。
 *       / When the millisecond timebase wraps around,
 *       `MillisecondTimestamp::operator-` uses this value to recover the elapsed
 *       time across the wrap. The default `UINT32_MAX` means the full range of a
 *       32-bit timebase.
 */
inline uint32_t libxr_timebase_max_valid_ms = UINT32_MAX;  // NOLINT

namespace LibXR
{

/**
 * @brief 微秒时间戳 / Microsecond timestamp
 */
class MicrosecondTimestamp
{
 public:
  /**
   * @brief 创建零值微秒时间戳 / Construct a zero microsecond timestamp
   */
  MicrosecondTimestamp() = default;

  /**
   * @brief 从微秒计数构造时间戳 / Construct a timestamp from microsecond ticks
   * @param microsecond 微秒计数值 / Microsecond tick count
   */
  MicrosecondTimestamp(uint64_t microsecond) : microsecond_(microsecond) {}

  /**
   * @brief 转换为微秒计数值 / Convert to the raw microsecond tick count
   * @return 微秒计数值 / Raw microsecond tick count
   */
  operator uint64_t() const { return microsecond_; }

  /**
   * @brief 微秒时长 / Duration in microseconds
   */
  class Duration
  {
   public:
    /**
     * @brief 从微秒差值构造时长 / Construct a duration from a microsecond delta
     * @param diff 微秒差值 / Microsecond delta
     */
    Duration(uint64_t diff) : diff_(diff) {}

    /**
     * @brief 转换为微秒差值 / Convert to the raw microsecond delta
     * @return 微秒差值 / Raw microsecond delta
     */
    operator uint64_t() const { return diff_; }

    /**
     * @brief 转换为秒 / Convert the duration to seconds
     * @return 秒 / Seconds
     */
    [[nodiscard]] double ToSecond() const
    {
      return static_cast<double>(diff_) / 1000000.0;
    }

    /**
     * @brief 转换为单精度秒 / Convert the duration to seconds in single precision
     * @return 单精度秒 / Seconds in single precision
     */
    [[nodiscard]] float ToSecondf() const
    {
      return static_cast<float>(diff_) / 1000000.0f;
    }

    /**
     * @brief 转换为微秒 / Convert the duration to microseconds
     * @return 微秒 / Microseconds
     */
    [[nodiscard]] uint64_t ToMicrosecond() const { return diff_; }

    /**
     * @brief 转换为毫秒 / Convert the duration to milliseconds
     * @return 毫秒 / Milliseconds
     */
    [[nodiscard]] uint32_t ToMillisecond() const { return diff_ / 1000u; }

   private:
    uint64_t diff_ = 0;
  };

  /**
   * @brief 计算时间差，支持时间基准回绕 / Compute elapsed time with timebase wraparound
   * @param old_timestamp 旧时间戳 / Older timestamp
   * @return 当前时间戳相对旧时间戳的时长 / Elapsed duration from the older timestamp
   *
   * @note 若当前时间戳小于旧时间戳，则按一次时间基准回绕处理，回绕上界由
   *       `libxr_timebase_max_valid_us` 指定。
   *       / If the current timestamp is smaller than the older one, the function
   *       treats it as one wraparound event whose upper bound is defined by
   *       `libxr_timebase_max_valid_us`.
   */
  [[nodiscard]] Duration operator-(const MicrosecondTimestamp& old_timestamp) const
  {
    uint64_t elapsed = 0;

    if (microsecond_ >= old_timestamp.microsecond_)
    {
      elapsed = microsecond_ - old_timestamp.microsecond_;
    }
    else
    {
      elapsed = microsecond_ + (libxr_timebase_max_valid_us - old_timestamp.microsecond_) +
                1ULL;
    }

    ASSERT(elapsed <= libxr_timebase_max_valid_us);

    return Duration(elapsed);
  }

  /**
   * @brief 复制赋值 / Copy-assign from another timestamp
   * @param other 源时间戳 / Source timestamp
   * @return 当前对象引用 / Reference to this object
   */
  MicrosecondTimestamp& operator=(const MicrosecondTimestamp& other) = default;

 private:
  uint64_t microsecond_ = 0;
};

/**
 * @brief 毫秒时间戳 / Millisecond timestamp
 */
class MillisecondTimestamp
{
 public:
  /**
   * @brief 创建零值毫秒时间戳 / Construct a zero millisecond timestamp
   */
  MillisecondTimestamp() = default;

  /**
   * @brief 从毫秒计数构造时间戳 / Construct a timestamp from millisecond ticks
   * @param millisecond 毫秒计数值 / Millisecond tick count
   */
  MillisecondTimestamp(uint32_t millisecond) : millisecond_(millisecond) {}

  /**
   * @brief 转换为毫秒计数值 / Convert to the raw millisecond tick count
   * @return 毫秒计数值 / Raw millisecond tick count
   */
  operator uint32_t() const { return millisecond_; }

  /**
   * @brief 毫秒时长 / Duration in milliseconds
   */
  class Duration
  {
   public:
    /**
     * @brief 从毫秒差值构造时长 / Construct a duration from a millisecond delta
     * @param diff 毫秒差值 / Millisecond delta
     */
    Duration(uint32_t diff) : diff_(diff) {}

    /**
     * @brief 转换为毫秒差值 / Convert to the raw millisecond delta
     * @return 毫秒差值 / Raw millisecond delta
     */
    operator uint32_t() const { return diff_; }

    /**
     * @brief 转换为秒 / Convert the duration to seconds
     * @return 秒 / Seconds
     */
    [[nodiscard]] double ToSecond() const
    {
      return static_cast<double>(diff_) / 1000.0;
    }

    /**
     * @brief 转换为单精度秒 / Convert the duration to seconds in single precision
     * @return 单精度秒 / Seconds in single precision
     */
    [[nodiscard]] float ToSecondf() const
    {
      return static_cast<float>(diff_) / 1000.0f;
    }

    /**
     * @brief 转换为毫秒 / Convert the duration to milliseconds
     * @return 毫秒 / Milliseconds
     */
    [[nodiscard]] uint32_t ToMillisecond() const { return diff_; }

    /**
     * @brief 转换为微秒 / Convert the duration to microseconds
     * @return 微秒 / Microseconds
     */
    [[nodiscard]] uint64_t ToMicrosecond() const
    {
      return static_cast<uint64_t>(diff_) * 1000u;
    }

   private:
    uint32_t diff_ = 0;
  };

  /**
   * @brief 计算时间差，支持时间基准回绕 / Compute elapsed time with timebase wraparound
   * @param old_timestamp 旧时间戳 / Older timestamp
   * @return 当前时间戳相对旧时间戳的时长 / Elapsed duration from the older timestamp
   *
   * @note 若当前时间戳小于旧时间戳，则按一次时间基准回绕处理，回绕上界由
   *       `libxr_timebase_max_valid_ms` 指定。
   *       / If the current timestamp is smaller than the older one, the function
   *       treats it as one wraparound event whose upper bound is defined by
   *       `libxr_timebase_max_valid_ms`.
   */
  [[nodiscard]] Duration operator-(const MillisecondTimestamp& old_timestamp) const
  {
    uint32_t elapsed = 0;

    if (millisecond_ >= old_timestamp.millisecond_)
    {
      elapsed = millisecond_ - old_timestamp.millisecond_;
    }
    else
    {
      elapsed =
          millisecond_ + (libxr_timebase_max_valid_ms - old_timestamp.millisecond_) + 1U;
    }

    ASSERT(elapsed <= libxr_timebase_max_valid_ms);

    return Duration(elapsed);
  }

 private:
  uint32_t millisecond_ = 0;
};

}  // namespace LibXR
