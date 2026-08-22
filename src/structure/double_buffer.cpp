#include "double_buffer.hpp"

#include "libxr_mem.hpp"

namespace LibXR
{

DoubleBuffer::DoubleBuffer(const RawData& raw_data) { Init(raw_data); }

void DoubleBufferStorage::Init(const RawData& raw_data)
{
  constexpr size_t ALIGN = alignof(size_t);
  const bool EMPTY_STORAGE = (raw_data.addr_ == nullptr) && (raw_data.size_ == 0U);

  ASSERT(EMPTY_STORAGE || (raw_data.addr_ != nullptr));

  if (!EMPTY_STORAGE)
  {
    ASSERT(raw_data.size_ > 0U);
    ASSERT((reinterpret_cast<uintptr_t>(raw_data.addr_) % ALIGN) == 0U);
    ASSERT((raw_data.size_ % (2U * ALIGN)) == 0U);
  }

  size_ = raw_data.size_ / 2U;
  buffer_[0] = static_cast<uint8_t*>(raw_data.addr_);
  buffer_[1] = EMPTY_STORAGE ? nullptr : (static_cast<uint8_t*>(raw_data.addr_) + size_);
  Reset();
}

void DoubleBufferStorage::Reset() { active_ = 0; }

uint8_t* DoubleBufferStorage::ActiveBuffer() const { return buffer_[active_]; }

uint8_t* DoubleBufferStorage::PendingBuffer() const { return buffer_[1 - active_]; }

uint8_t* DoubleBufferStorage::Buffer(int block) const
{
  ASSERT((block == 0) || (block == 1));
  return buffer_[block];
}

size_t DoubleBufferStorage::Size() const { return size_; }

void DoubleBuffer::Init(const RawData& raw_data)
{
  storage_.Init(raw_data);
  Reset();
}

void DoubleBuffer::Reset()
{
  storage_.Reset();
  pending_valid_ = false;
  active_len_ = 0U;
  pending_len_ = 0U;
}

uint8_t* DoubleBuffer::ActiveBuffer() const { return storage_.ActiveBuffer(); }

uint8_t* DoubleBuffer::PendingBuffer() const { return storage_.PendingBuffer(); }

uint8_t* DoubleBuffer::Buffer(int block) const { return storage_.Buffer(block); }

size_t DoubleBuffer::Size() const { return storage_.Size(); }

void DoubleBuffer::Switch()
{
  if (pending_valid_)
  {
    storage_.FlipActiveBlock();
    pending_valid_ = false;
  }
}

bool DoubleBuffer::HasPending() const { return pending_valid_; }

void DoubleBuffer::EnablePending() { pending_valid_ = true; }

size_t DoubleBuffer::GetPendingLength() const
{
  return pending_valid_ ? pending_len_ : 0U;
}

bool DoubleBuffer::FillPending(const uint8_t* data, size_t len)
{
  if (pending_valid_ || len > storage_.Size())
  {
    return false;
  }

  Memory::FastCopy(storage_.PendingBuffer(), data, len);
  pending_len_ = len;
  pending_valid_ = true;
  return true;
}

bool DoubleBuffer::FillActive(const uint8_t* data, size_t len)
{
  if (len > storage_.Size())
  {
    return false;
  }

  Memory::FastCopy(storage_.ActiveBuffer(), data, len);
  return true;
}

}  // namespace LibXR
