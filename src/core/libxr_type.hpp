#pragma once

#include "libxr_def.hpp"

namespace LibXR {
class ConstRawData;

class RawData {
public:
  RawData(void *addr, size_t size) : addr_(addr), size_(size) {}
  RawData() : addr_(nullptr), size_(0){};
  template <typename DataType>
  RawData(DataType &data) : addr_(&data), size_(sizeof(DataType)) {}
  RawData(RawData &data) = default;

  void *addr_;
  size_t size_;
};

class ConstRawData {
public:
  ConstRawData(const void *addr, size_t size) : addr_(addr), size_(size) {}
  ConstRawData() : addr_(nullptr), size_(0){};
  template <typename DataType>
  ConstRawData(const DataType &data) : addr_(&data), size_(sizeof(DataType)) {}
  ConstRawData(ConstRawData &data) = default;
  ConstRawData(RawData &data) : addr_(data.addr_), size_(data.size_) {}

  const void *addr_;
  size_t size_;
};

} // namespace LibXR
