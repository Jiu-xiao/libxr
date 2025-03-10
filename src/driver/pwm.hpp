#pragma once

#include "libxr.hpp"

namespace LibXR {

class PWM {
 public:
  PWM() = default;

  typedef struct {
    uint32_t frequency;
  } Configuration;

  virtual ErrorCode SetDutyCycle(float value) = 0;

  virtual ErrorCode SetConfig(Configuration config) = 0;

  virtual ErrorCode Enable() = 0;
  virtual ErrorCode Disable() = 0;
};
}  // namespace LibXR
