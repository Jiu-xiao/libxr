#pragma once

#include "libxr_time.hpp"

namespace LibXR {
class Timebase {
 public:
  Timebase() { timebase = this; }

  static TimestampUS GetMicroseconds() { return timebase->_get_microseconds(); }
  static TimestampMS GetMilliseconds() { return timebase->_get_milliseconds(); }

  virtual TimestampUS _get_microseconds() = 0;  // NOLINT
  virtual TimestampMS _get_milliseconds() = 0;  // NOLINT

  static Timebase *timebase;  // NOLINT
};
}  // namespace LibXR
