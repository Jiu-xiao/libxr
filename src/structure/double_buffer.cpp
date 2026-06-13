#include "double_buffer.hpp"

#include "libxr_mem.hpp"

using namespace LibXR;

DoubleBuffer::DoubleBuffer(const LibXR::RawData& raw_data)
{
  Init(raw_data);
}

void DoubleBuffer::Init(const LibXR::RawData& raw_data)
{
  assert(raw_data.addr_ != nullptr);
  assert((raw_data.size_ % 2U) == 0U);
  assert(raw_data.size_ > 0U);

  size_ = raw_data.size_ / 2U;
  buffer_[0] = static_cast<uint8_t*>(raw_data.addr_);
  buffer_[1] = static_cast<uint8_t*>(raw_data.addr_) + size_;
  active_ = 0;
  pending_valid_ = false;
  active_len_ = 0;
  pending_len_ = 0;
}

uint8_t* DoubleBuffer::ActiveBuffer() const { return buffer_[active_]; }

uint8_t* DoubleBuffer::PendingBuffer() const { return buffer_[1 - active_]; }

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
  LibXR::Memory::FastCopy(PendingBuffer(), data, len);
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
  LibXR::Memory::FastCopy(ActiveBuffer(), data, len);
  return true;
}

void DoubleBuffer::EnablePending() { pending_valid_ = true; }

size_t DoubleBuffer::GetPendingLength() const
{
  return pending_valid_ ? pending_len_ : 0;
}
