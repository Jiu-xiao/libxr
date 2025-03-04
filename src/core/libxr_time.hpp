#pragma once

#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

namespace LibXR {
class TimestampUS {
public:
  TimestampUS() : microsecond_(0) {}
  TimestampUS(uint64_t microsecond) : microsecond_(microsecond) {}
  operator uint64_t() const { return microsecond_; }

  class TimeDiffUS {
  public:
    TimeDiffUS(uint64_t diff) : diff_(diff) {}

    operator uint64_t() const { return diff_; }

    double to_second() const { return static_cast<double>(diff_) / 1000000.0; }
    float to_secondf() const { return static_cast<float>(diff_) / 1000000.0f; }

    uint64_t to_microsecond() const { return diff_; }

    uint64_t to_millisecond() const { return diff_ / 1000u; }

  private:
    uint64_t diff_;
  };

  TimeDiffUS operator-(const TimestampUS &old_microsecond) const {
    ASSERT(microsecond_ >= old_microsecond.microsecond_);
    return TimeDiffUS(microsecond_ - old_microsecond.microsecond_);
  }

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  TimestampUS(T microsecond)
      : microsecond_(static_cast<uint64_t>(microsecond)) {}

  TimestampUS &operator=(const TimestampUS &other) {
    if (this != &other) {
      microsecond_ = other.microsecond_;
    }
    return *this;
  }

  TimestampUS &operator=(TimestampUS &&other) noexcept {
    if (this != &other) {
      microsecond_ = other.microsecond_;
      other.microsecond_ = 0;
    }
    return *this;
  }

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  TimestampUS &operator=(T value) {
    microsecond_ = static_cast<uint64_t>(value);
    return *this;
  }

private:
  uint64_t microsecond_;
};

class TimestampMS {
public:
  TimestampMS() : millisecond_(0) {}
  TimestampMS(uint32_t millisecond) : millisecond_(millisecond) {}
  operator uint32_t() const { return millisecond_; }
  TimestampMS(TimestampUS &microsecond) { millisecond_ = microsecond / 1000u; }

  class TimeDiffMS {
  public:
    TimeDiffMS(uint32_t diff) : diff_(diff) {}

    operator uint32_t() const { return diff_; }

    double to_second() { return static_cast<double>(diff_) / 1000.0; }

    float to_secondf() { return static_cast<float>(diff_) / 1000.0f; }

    uint32_t to_millisecond() const { return diff_; }

    uint32_t to_microsecond() const { return diff_ * 1000u; }

  private:
    uint32_t diff_;
  };

  TimeDiffMS operator-(TimestampMS &old_millisecond) {
    ASSERT(millisecond_ >= old_millisecond);
    return TimeDiffMS(millisecond_ - old_millisecond);
  }

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  TimestampMS(T millisecond)
      : millisecond_(static_cast<uint32_t>(millisecond)) {}

  TimestampMS(const TimestampUS &microsecond)
      : millisecond_(static_cast<uint32_t>(microsecond / 1000u)) {}

  TimestampMS &operator=(const TimestampMS &other) {
    if (this != &other) {
      millisecond_ = other.millisecond_;
    }
    return *this;
  }

  TimestampMS &operator=(TimestampMS &&other) noexcept {
    if (this != &other) {
      millisecond_ = other.millisecond_;
      other.millisecond_ = 0;
    }
    return *this;
  }

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  TimestampMS &operator=(T value) {
    millisecond_ = static_cast<uint32_t>(value);
    return *this;
  }

private:
  uint32_t millisecond_;
};
} // namespace LibXR