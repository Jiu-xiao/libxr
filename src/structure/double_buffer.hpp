#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "double_buffer_storage.hpp"

namespace LibXR
{

/**
 * @brief Full active/pending double-buffer manager.
 *
 * This class owns the storage view and the generic pending/length bookkeeping used by
 * USB, SPI, ESP helpers, and the standalone structure tests. State machines with
 * different commit or failure semantics should use DoubleBufferStorage directly.
 */
class DoubleBuffer
{
 public:
  /**
   * @brief Constructs an uninitialized double-buffer manager.
   */
  DoubleBuffer() = default;

  /**
   * @brief Constructs a manager over one continuous two-block storage region.
   * @param raw_data Continuous storage; an empty buffer may use `nullptr + 0`.
   */
  explicit DoubleBuffer(const RawData& raw_data);

  /**
   * @brief Binds continuous backing storage and resets all active/pending state.
   */
  void Init(const RawData& raw_data);

  /**
   * @brief Resets active/pending state while retaining the backing storage.
   */
  void Reset();

  /** @brief Returns the currently active buffer. */
  uint8_t* ActiveBuffer() const;

  /** @brief Returns the inactive buffer available for pending data. */
  uint8_t* PendingBuffer() const;

  /**
   * @brief Returns a numbered backing block.
   * @param block Block index; only 0 and 1 are valid.
   */
  uint8_t* Buffer(int block) const;

  /** @brief Returns the size of each backing block in bytes. */
  size_t Size() const;

  /**
   * @brief Switches to the pending block when pending data has been enabled.
   */
  void Switch();

  /** @brief Returns whether pending data is ready to switch. */
  bool HasPending() const;

  /**
   * @brief Copies data into the pending block and marks it valid.
   * @return False when pending is already valid or the length exceeds the block size.
   */
  bool FillPending(const uint8_t* data, size_t len);

  /**
   * @brief Copies data into the active block without changing pending state.
   * @return False when the length exceeds the block size.
   */
  bool FillActive(const uint8_t* data, size_t len);

  /**
   * @brief Marks manually populated pending data as valid.
   */
  void EnablePending();

  /** @brief Returns the valid pending length, or zero when pending is invalid. */
  size_t GetPendingLength() const;

  /** @brief Returns the valid length associated with the active block. */
  size_t GetActiveLength() const { return active_len_; }

  /** @brief Sets the length associated with pending data. */
  void SetPendingLength(size_t length) { pending_len_ = length; }

  /** @brief Sets the length associated with active data. */
  void SetActiveLength(size_t length) { active_len_ = length; }

  /** @brief Flips the active block index without changing length bookkeeping. */
  void FlipActiveBlock() { storage_.FlipActiveBlock(); }

  /** @brief Returns the current active block index. */
  int ActiveBlock() const { return storage_.ActiveBlock(); }

  /**
   * @brief Selects the initial active block.
   * @param block True selects block 1; false selects block 0.
   */
  void SetActiveBlock(bool block) { storage_.SetActiveBlock(block); }

 private:
  DoubleBufferStorage storage_{};
  bool pending_valid_ = false;
  size_t active_len_ = 0U;
  size_t pending_len_ = 0U;
};

}  // namespace LibXR
