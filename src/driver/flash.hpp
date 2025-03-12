#pragma once

#include <cstdint>

#include "libxr_type.hpp"

namespace LibXR {

class Flash {
 public:
  Flash(size_t min_erase_size, size_t min_write_size, RawData flash_area)
      : min_erase_size_(min_erase_size),
        min_write_size_(min_write_size),
        flash_area_(flash_area) {}

  size_t min_erase_size_ = 0;
  size_t min_write_size_ = 0;
  RawData flash_area_;

  virtual ErrorCode Erase(size_t offset, size_t size) = 0;
  virtual ErrorCode Write(size_t offset, ConstRawData data) = 0;
};
}  // namespace LibXR
