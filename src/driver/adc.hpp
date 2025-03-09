#pragma once

#include "libxr.hpp"

namespace LibXR {

class ADC {
 public:
  ADC() = default;

  virtual float Read() = 0;
};
}  // namespace LibXR
