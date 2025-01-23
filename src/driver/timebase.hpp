#pragma once

#include "libxr.hpp"

namespace LibXR {
class Timebase {
public:
  Timebase() { timebase = this; }

  static TimestampUS GetMicroseconds() { return timebase->_get_microseconds(); }
  static TimestampMS GetMilliseconds() { return timebase->_get_milliseconds(); }

  virtual TimestampUS _get_microseconds() = 0;
  virtual TimestampMS _get_milliseconds() = 0;

  static Timebase *timebase;
};
} // namespace LibXR
