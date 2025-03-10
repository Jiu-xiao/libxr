#pragma once

#include "libxr.hpp"
#include "libxr_rw.hpp"

namespace LibXR {
class UART {
 public:
  enum class Parity : uint8_t { NO_PARITY = 0, EVEN = 1, ODD = 2 };

  struct Configuration {
    uint32_t baudrate;
    Parity parity;
    uint8_t data_bits;
    uint8_t stop_bits;
  };

  ReadPort read_port_;
  WritePort write_port_;

  UART(ReadPort read_port = ReadPort(), WritePort write_port = WritePort())
      : read_port_(read_port), write_port_(write_port) {}

  virtual ErrorCode SetConfig(Configuration config) = 0;
};
}  // namespace LibXR
