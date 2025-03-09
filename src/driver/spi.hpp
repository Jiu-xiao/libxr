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

  SPI(size_t queue_size = 3, size_t buffer_size = 128)
      : queue_block_(new LockFreeQueue<ReadInfoBlock>(queue_size)),
        queue_data_(new BaseQueue(1, buffer_size)) {}

  LockFreeQueue<ReadInfoBlock> *queue_block_ = nullptr;

  BaseQueue *queue_data_ = nullptr;

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
