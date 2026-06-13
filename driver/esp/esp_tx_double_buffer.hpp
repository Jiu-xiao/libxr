#pragma once

#include <cstddef>
#include <cstdint>
#include "double_buffer.hpp"
#include "flag.hpp"
#include "libxr_assert.hpp"
#include "operation.hpp"

namespace LibXR
{

class ESPTxDoubleBuffer
{
 public:
  ESPTxDoubleBuffer() = default;

  void Init(RawData storage)
  {
    ASSERT(storage.addr_ != nullptr);
    ASSERT((storage.size_ % 2U) == 0U);
    ASSERT(storage.size_ > 0U);

    bytes_.Init(storage);
    Reset();
  }

  void Reset()
  {
    active_block_ = false;
    bytes_.SetActiveBlock(false);
    ClearActive();
    ClearPending();
  }

  uint8_t* ActiveBuffer() const
  {
    return bytes_.ActiveBuffer();
  }

  uint8_t* PendingBuffer() const
  {
    return bytes_.PendingBuffer();
  }

  size_t BufferSize() const
  {
    return bytes_.Size();
  }

  bool HasActive() const { return active_valid_; }

  bool HasPending() const { return pending_valid_.IsSet(); }

  size_t ActiveLength() const { return active_length_; }

  size_t ActiveOffset() const { return active_offset_; }

  void SetActiveOffset(size_t offset) { active_offset_ = offset; }

  WriteInfoBlock& ActiveInfo() { return active_info_; }

  const WriteInfoBlock& ActiveInfo() const { return active_info_; }

  WriteInfoBlock& PendingInfo() { return pending_info_; }

  const WriteInfoBlock& PendingInfo() const { return pending_info_; }

  void LoadActive(size_t size, const WriteInfoBlock& info)
  {
    active_length_ = size;
    active_offset_ = 0U;
    active_info_ = info;
    active_valid_ = true;
  }

  void LoadPending(size_t size, const WriteInfoBlock& info)
  {
    pending_length_ = size;
    pending_info_ = info;
    pending_valid_.Set();
  }

  bool PromotePending()
  {
    if (!pending_valid_.TestAndClear())
    {
      return false;
    }

    active_block_ = !active_block_;
    bytes_.SetActiveBlock(active_block_);
    active_length_ = pending_length_;
    active_offset_ = 0U;
    active_info_ = pending_info_;
    active_valid_ = true;
    pending_length_ = 0U;
    pending_info_ = {};
    return true;
  }

  void ClearActive()
  {
    active_length_ = 0U;
    active_offset_ = 0U;
    active_info_ = {};
    active_valid_ = false;
  }

  void ClearPending()
  {
    pending_valid_.Clear();
    pending_length_ = 0U;
    pending_info_ = {};
  }

 private:
  DoubleBuffer bytes_{};
  bool active_block_ = false;
  size_t active_length_ = 0U;
  size_t pending_length_ = 0U;
  size_t active_offset_ = 0U;
  WriteInfoBlock active_info_ = {};
  WriteInfoBlock pending_info_ = {};
  bool active_valid_ = false;
  Flag::Atomic pending_valid_{};
};

}  // namespace LibXR
