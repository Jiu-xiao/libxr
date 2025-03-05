#pragma once

#include <cstring>
#include <string>

#include "libxr_def.hpp"

namespace LibXR {
class RawData;
class ConstRawData;

class RawData {
public:
  RawData(void *addr, size_t size) : addr_(addr), size_(size) {}
  RawData() : addr_(nullptr), size_(0) {}

  template <typename DataType>
  RawData(const DataType &data)
      : addr_(const_cast<DataType *>(&data)), size_(sizeof(DataType)) {}

  RawData(const RawData &data) = default;

  RawData(char *data) : addr_(data), size_(data ? strlen(data) : 0) {}

  template <size_t N>
  RawData(const char (&data)[N]) : addr_(&data), size_(N - 1) {}

  RawData(const std::string &data)
      : addr_(const_cast<char *>(data.data())), size_(data.size()) {}

  RawData &operator=(const RawData &data) = default;

  void *addr_;
  size_t size_;
};

class ConstRawData {
public:
  ConstRawData(const void *addr, size_t size) : addr_(addr), size_(size) {}
  ConstRawData() : addr_(nullptr), size_(0) {}

  template <typename DataType>
  ConstRawData(const DataType &data)
      : addr_(const_cast<DataType *>(&data)), size_(sizeof(DataType)) {}

  ConstRawData(const ConstRawData &data) = default;
  ConstRawData(const RawData &data) : addr_(data.addr_), size_(data.size_) {}

  ConstRawData(char *data) : addr_(data), size_(data ? strlen(data) : 0) {}

  ConstRawData(const char *data)
      : addr_(data), size_(data ? strlen(data) : 0) {}

  template <size_t N>
  ConstRawData(const char (&data)[N]) : addr_(data), size_(N - 1) {}

  ConstRawData &operator=(const ConstRawData &data) = default;

  const void *addr_;
  size_t size_;
};

class Buffer {
public:
  Buffer(size_t size) : size_(size), used_(0), raw_(new uint8_t[size]) {}
  Buffer(const RawData &data)
      : size_(data.size_), used_(0),
        raw_(data.addr_ ? static_cast<uint8_t *>(data.addr_) : nullptr) {}

  uint8_t operator[](size_t index) { return raw_[index]; }

  template <typename Data> Data toData(size_t index) {
    return reinterpret_cast<Data &>(raw_[index]);
  }

  ErrorCode operator=(const ConstRawData data) {
    if (data.size_ > size_) {
      return ErrorCode::SIZE_ERR;
    }

    std::memcpy(raw_, data.addr_, data.size_);
    used_ = data.size_;

    return ErrorCode::OK;
  }

  ErrorCode operator=(RawData data) { return *this = ConstRawData(data); }

  explicit operator uint8_t *() { return raw_; }

  explicit operator void *() { return raw_; }

  size_t Size() const { return size_; }

  size_t Used() const { return used_; }

  uint32_t size_, used_;
  uint8_t *raw_;
};

} // namespace LibXR
