#include "flash.hpp"

using namespace LibXR;

Flash::Flash(size_t min_erase_size, size_t min_write_size, RawData flash_area)
    : min_erase_size_(min_erase_size),
      min_write_size_(min_write_size),
      flash_area_(flash_area)
{
}

ErrorCode Flash::Read(size_t offset, RawData data)
{
  ASSERT(offset + data.size_ <= flash_area_.size_);
  memcpy(data.addr_, reinterpret_cast<const uint8_t*>(flash_area_.addr_) + offset,
         data.size_);

  return ErrorCode::OK;
}
