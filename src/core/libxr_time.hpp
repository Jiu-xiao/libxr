#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"

namespace LibXR {
class TimestampUS {
public:
  TimestampUS() : microsecond_(0) {}
  TimestampUS(uint64_t microsecond) : microsecond_(microsecond) {}
  operator uint64_t() { return microsecond_; }

  class TimeDiffUS {
  public:
    TimeDiffUS(uint64_t diff) : diff_(diff) { ASSERT(diff_ > UINT64_MAX / 2); }

    operator uint64_t() { return diff_; }

    double to_second() { return diff_ / 1000000.0; }

    float to_secondf() { return diff_ / 1000000.0f; }

  private:
    uint64_t diff_;
  };

  TimeDiffUS operator-(TimestampUS &old_microsecond) {
    return TimeDiffUS(microsecond_ - old_microsecond);
  }

private:
  uint64_t microsecond_;
};

class TimestampMS {
public:
  TimestampMS() : millisecond_(0) {}
  TimestampMS(uint32_t millisecond) : millisecond_(millisecond) {}
  operator uint32_t() { return millisecond_; }
  TimestampMS(TimestampUS &microsecond) { millisecond_ = microsecond / 1000u; }

  class TimeDiffMS {
  public:
    TimeDiffMS(uint32_t diff) : diff_(diff) { ASSERT(diff_ > UINT32_MAX / 2); }

    operator uint32_t() { return diff_; }

    double to_second() { return diff_ / 1000.0; }

    float to_secondf() { return diff_ / 1000.0f; }

  private:
    uint32_t diff_;
  };

  TimeDiffMS operator-(TimeDiffMS &old_millisecond) {
    return TimeDiffMS(millisecond_ - old_millisecond);
  }

private:
  uint32_t millisecond_;
};
} // namespace LibXR