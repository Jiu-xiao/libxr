#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

namespace LibXR {
class RawData {
public:
  RawData(void *addr, size_t size) : addr_(addr), size_(size) {}
  RawData() : addr_(NULL), size_(0){};
  template <typename DataType>
  RawData(DataType &data) : addr_(&data), size_(sizeof(DataType)) {}
  RawData(RawData &data) = default;

  void *addr_;
  size_t size_;
};

} // namespace LibXR
