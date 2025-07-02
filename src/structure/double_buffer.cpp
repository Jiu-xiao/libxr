#include "double_buffer.hpp"

using namespace LibXR;

DoubleBuffer::DoubleBuffer(const LibXR::RawData& raw_data) : size_(raw_data.size_ / 2)
{
  buffer_[0] = static_cast<uint8_t*>(raw_data.addr_);
  buffer_[1] = static_cast<uint8_t*>(raw_data.addr_) + size_;
}

uint8_t* DoubleBuffer::ActiveBuffer() { return buffer_[active_]; }

uint8_t* DoubleBuffer::PendingBuffer() { return buffer_[1 - active_]; }

size_t DoubleBuffer::Size() const { return size_; }

void DoubleBuffer::Switch()
{
  if (pending_valid_)
  {
    active_ ^= 1;
    pending_valid_ = false;
  }
}

bool DoubleBuffer::HasPending() const { return pending_valid_; }

bool DoubleBuffer::FillPending(const uint8_t* data, size_t len)
{
  if (pending_valid_ || len > size_)
  {
    return false;
  }
  std::memcpy(PendingBuffer(), data, len);
  pending_len_ = len;
  pending_valid_ = true;
  return true;
}

bool DoubleBuffer::FillActive(const uint8_t* data, size_t len)
{
  if (len > size_)
  {
    return false;
  }
  std::memcpy(ActiveBuffer(), data, len);
  return true;
}

void DoubleBuffer::EnablePending() { pending_valid_ = true; }

size_t DoubleBuffer::PendingLength() const { return pending_valid_ ? pending_len_ : 0; }
