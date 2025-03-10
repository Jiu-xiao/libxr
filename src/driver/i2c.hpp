#pragma once

#include "libxr.hpp"

namespace LibXR {
class I2C {
 public:
  struct Configuration {
    uint32_t clock_speed;
  };

  I2C() {}

  virtual ErrorCode Read(uint16_t slave_addr, RawData read_data,
                         ReadOperation &op) = 0;
  virtual ErrorCode Write(uint16_t slave_addr, ConstRawData write_data,
                          WriteOperation &op) = 0;

  virtual ErrorCode SetConfig(Configuration config) = 0;
};
}  // namespace LibXR
