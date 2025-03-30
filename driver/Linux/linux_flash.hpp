#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR {

template <size_t FLASH_SIZE>
class LinuxBinaryFileFlash : public Flash {
 public:
  LinuxBinaryFileFlash(const std::string& file_path,
                       size_t min_erase_size = FLASH_SIZE / 2,
                       size_t min_write_size = sizeof(uint8_t),
                       bool write_order_check = false,
                       bool write_as_one_check = false)
      : Flash(min_erase_size, min_write_size,
              RawData(&flash_area_, sizeof(flash_area_))),
        file_path_(file_path),
        write_order_check_(write_order_check),
        write_as_one_check_(write_as_one_check) {
    std::ifstream file(file_path_, std::ios::binary);
    if (file) {
      file.read(reinterpret_cast<char*>(flash_area_.data()),
                static_cast<std::streamsize>(flash_area_.size()));
    }
  }

  ErrorCode Erase(size_t offset, size_t size) override {
    ASSERT(offset % min_erase_size_ == 0);
    ASSERT(size % min_erase_size_ == 0);

    if ((offset + size) > flash_area_.size()) {
      return ErrorCode::OUT_OF_RANGE;
    }

    std::memset(flash_area_.data() + offset, 0xFF, size);

    return SyncToFile();
  }

  ErrorCode Write(size_t offset, ConstRawData data) override {
    if ((offset + data.size_) > flash_area_.size()) {
      return ErrorCode::OUT_OF_RANGE;
    }

    if (offset % min_write_size_ != 0 || data.size_ % min_write_size_ != 0) {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    if (write_order_check_) {
      ASSERT(offset % min_erase_size_ == 0);
    }

    uint8_t* dst = flash_area_.data() + offset;
    const uint8_t* src = static_cast<const uint8_t*>(data.addr_);

    if (write_as_one_check_) {
      for (size_t i = 0; i < data.size_; ++i) {
        if ((~dst[i] & src[i])) {
          ASSERT(false);
          return ErrorCode::FAILED;
        }
      }
    }

    std::memcpy(dst, src, data.size_);
    return SyncToFile();
  }

 private:
  std::string file_path_;
  std::array<uint8_t, FLASH_SIZE> flash_area_ = {};
  bool write_order_check_;
  bool write_as_one_check_;

  ErrorCode SyncToFile() {
    std::ofstream file(file_path_, std::ios::binary | std::ios::trunc);
    if (!file) {
      return ErrorCode::FAILED;
    }
    file.write(reinterpret_cast<const char*>(flash_area_.data()),
               static_cast<std::streamsize>(flash_area_.size()));
    return ErrorCode::OK;
  }
};
}  // namespace LibXR
