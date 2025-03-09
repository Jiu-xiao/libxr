#pragma once

#include "libxr.hpp"

namespace LibXR {

class PowerManager {
 public:
  PowerManager() = default;
  virtual ~PowerManager() = default;

  virtual void Reset() = 0;
  virtual void Shutdown() = 0;
};

}  // namespace LibXR
