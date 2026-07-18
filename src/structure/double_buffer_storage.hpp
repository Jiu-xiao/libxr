#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_type.hpp"

namespace LibXR
{

/**
 * @brief Stateless two-block storage view used by active/pending state machines.
 *
 * This class owns only the two backing-block pointers, the half-buffer size, and the
 * active-block index. It does not track pending validity or either block's data length.
 * Those semantics belong to the caller that owns the corresponding state machine.
 */
class DoubleBufferStorage
{
 public:
  DoubleBufferStorage() = default;
  explicit DoubleBufferStorage(const RawData& raw_data) { Init(raw_data); }

  void Init(const RawData& raw_data);
  void Reset();

  [[nodiscard]] uint8_t* ActiveBuffer() const;
  [[nodiscard]] uint8_t* PendingBuffer() const;
  [[nodiscard]] uint8_t* Buffer(int block) const;
  [[nodiscard]] size_t Size() const;

  void FlipActiveBlock() { active_ ^= 1; }
  [[nodiscard]] int ActiveBlock() const { return active_; }
  void SetActiveBlock(bool block) { active_ = block ? 1 : 0; }

 private:
  uint8_t* buffer_[2] = {nullptr, nullptr};
  size_t size_ = 0U;
  int active_ = 0;
};

}  // namespace LibXR
