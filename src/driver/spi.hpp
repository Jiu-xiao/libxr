#pragma once

#include "libxr.hpp"

namespace LibXR {
class SPI {
 public:
  enum class ClockPolarity : uint8_t { LOW = 0, HIGH = 1 };
  enum class ClockPhase : uint8_t { EDGE_1 = 0, EDGE_2 = 1 };

  using OperationRW = WriteOperation;

  struct Configuration {
    ClockPolarity clock_polarity;
    ClockPhase clock_phase;
  };

  struct ReadWriteInfo {
    RawData read_data;
    ConstRawData write_data;
    OperationRW op;
  };

  SPI() {}

  virtual ErrorCode ReadAndWrite(RawData read_data, ConstRawData write_data,
                                 OperationRW &op) = 0;

  virtual ErrorCode Read(RawData read_data, OperationRW &op) {
    return ReadAndWrite(read_data, ConstRawData(nullptr, 0), op);
  }

  virtual ErrorCode Write(ConstRawData write_data, OperationRW &op) {
    return ReadAndWrite(RawData(nullptr, 0), write_data, op);
  }

  virtual ErrorCode SetConfig(Configuration config) = 0;
};
}  // namespace LibXR
