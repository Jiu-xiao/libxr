#pragma once

#include "libxr.hpp"

namespace LibXR {
class GPIO {
 public:
  enum class Direction : uint8_t {
    INPUT,
    OUTPUT_PUSH_PULL,
    OUTPUT_OPEN_DRAIN,
    FALL_INTERRUPT,
    RISING_INTERRUPT,
    FALL_RISING_INTERRUPT
  };
  enum class Pull : uint8_t { NONE, UP, DOWN };

  struct Configuration {
    Direction direction;
    Pull pull;
  };

  Callback<> callback_;

  GPIO() {}

  virtual bool Read() = 0;

  virtual ErrorCode Write(bool value) = 0;

  virtual ErrorCode EnableInterrupt() = 0;

  virtual ErrorCode DisableInterrupt() = 0;

  virtual ErrorCode SetConfig(Configuration config) = 0;

  ErrorCode RegisterCallback(Callback<> callback) {
    callback_ = callback;
    return ErrorCode::OK;
  }
};
}  // namespace LibXR
