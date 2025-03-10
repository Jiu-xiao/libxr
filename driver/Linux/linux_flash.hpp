#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_type.hpp"

namespace LibXR {

template <size_t FLASH_SIZE>
class LinuxBinaryFileFlash : public Flash {
 public:
  LinuxBinaryFileFlash(size_t min_erase_size, const std::string& file_path)
      : Flash(min_erase_size, RawData(flash_area_.data(), flash_area_.size())),
        file_path_(file_path) {
    std::ifstream file(file_path_, std::ios::binary);
    if (file) {
      file.read(reinterpret_cast<char*>(flash_area_.data()),
                static_cast<std::streamsize>(flash_area_.size()));
    }
  }

  ErrorCode Erase(size_t offset, size_t size) override {
    if ((offset % min_erase_size_) != 0 || (size % min_erase_size_) != 0) {
      return ErrorCode::ARG_ERR;
    }
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
    uint8_t* dst = flash_area_.data() + offset;
    const uint8_t* src = static_cast<const uint8_t*>(data.addr_);
    for (size_t i = 0; i < data.size_; ++i) {
      if ((dst[i] & src[i]) != src[i]) {
        return ErrorCode::FAILED;
      }
    }
    std::memcpy(dst, src, data.size_);
    return SyncToFile();
  }

 private:
  std::string file_path_;
  std::array<uint8_t, FLASH_SIZE> flash_area_ = {};

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
