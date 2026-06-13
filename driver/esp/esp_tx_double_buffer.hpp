#pragma once

#include <cstddef>
#include <cstdint>
#include "double_buffer.hpp"
#include "flag.hpp"
#include "libxr_assert.hpp"
#include "operation.hpp"

namespace LibXR
{

/**
 * @brief ESP 发送路径双缓冲辅助器。
 * @brief Double-buffer helper for ESP transmit paths.
 *
 * 该辅助器复用 `DoubleBuffer` 管理 payload 缓冲区，并额外维护
 * active/pending 两个发送请求的长度、偏移和 `WriteInfoBlock` 元数据。
 * It reuses `DoubleBuffer` for payload storage and additionally tracks
 * active/pending request length, offset, and `WriteInfoBlock` metadata.
 */
class ESPTxDoubleBuffer
{
 public:
  /**
   * @brief 默认构造辅助器。
   * @brief Construct one helper in the uninitialized state.
   */
  ESPTxDoubleBuffer() = default;

  /**
   * @brief 绑定外部双缓冲 backing storage 并重置状态。
   * @brief Bind external double-buffer backing storage and reset state.
   *
   * backing storage 的合法性约束由 `DoubleBuffer::Init()` 统一负责。
   * The backing-storage contract is enforced solely by `DoubleBuffer::Init()`.
   *
   * @param storage 外部双缓冲 backing storage。
   * @param storage External double-buffer backing storage.
   */
  void Init(RawData storage)
  {
    bytes_.Init(storage);
    Reset();
  }

  /**
   * @brief 重置当前 active/pending 发送状态。
   * @brief Reset the current active/pending transmit state.
   */
  void Reset()
  {
    bytes_.SetActiveBlock(false);
    ClearActive();
    ClearPending();
  }

  /**
   * @brief 获取当前 active payload 缓冲区指针。
   * @brief Return the current active payload buffer pointer.
   */
  uint8_t* ActiveBuffer() const
  {
    return bytes_.ActiveBuffer();
  }

  /**
   * @brief 获取当前 pending payload 缓冲区指针。
   * @brief Return the current pending payload buffer pointer.
   */
  uint8_t* PendingBuffer() const
  {
    return bytes_.PendingBuffer();
  }

  /**
   * @brief 获取单个半缓冲区大小。
   * @brief Return the size of one half-buffer.
   */
  size_t BufferSize() const
  {
    return bytes_.Size();
  }

  /**
   * @brief 判断是否存在有效 active 请求。
   * @brief Check whether a valid active request exists.
   */
  bool HasActive() const { return active_valid_; }

  /**
   * @brief 判断是否存在有效 pending 请求。
   * @brief Check whether a valid pending request exists.
   */
  bool HasPending() const { return pending_valid_.IsSet(); }

  /**
   * @brief 获取当前 active payload 长度。
   * @brief Return the current active payload length.
   */
  size_t ActiveLength() const { return active_length_; }

  /**
   * @brief 获取当前 active payload 已推进的偏移量。
   * @brief Return the current progressed offset of the active payload.
   */
  size_t ActiveOffset() const { return active_offset_; }

  /**
   * @brief 更新当前 active payload 已推进的偏移量。
   * @brief Update the progressed offset of the active payload.
   *
   * @param offset 新的 active 偏移量。
   * @param offset New active offset.
   */
  void SetActiveOffset(size_t offset) { active_offset_ = offset; }

  /**
   * @brief 获取 active 请求元数据引用。
   * @brief Return the active request metadata reference.
   */
  WriteInfoBlock& ActiveInfo() { return active_info_; }

  /**
   * @brief 获取 active 请求元数据常量引用。
   * @brief Return the const active request metadata reference.
   */
  const WriteInfoBlock& ActiveInfo() const { return active_info_; }

  /**
   * @brief 获取 pending 请求元数据引用。
   * @brief Return the pending request metadata reference.
   */
  WriteInfoBlock& PendingInfo() { return pending_info_; }

  /**
   * @brief 获取 pending 请求元数据常量引用。
   * @brief Return the const pending request metadata reference.
   */
  const WriteInfoBlock& PendingInfo() const { return pending_info_; }

  /**
   * @brief 装载一个 active 请求的长度与元数据。
   * @brief Load the length and metadata of one active request.
   *
   * @param size active payload 长度。
   * @param size Active payload length.
   * @param info active 请求元数据。
   * @param info Active request metadata.
   */
  void LoadActive(size_t size, const WriteInfoBlock& info)
  {
    active_length_ = size;
    active_offset_ = 0U;
    active_info_ = info;
    active_valid_ = true;
  }

  /**
   * @brief 装载一个 pending 请求的长度与元数据。
   * @brief Load the length and metadata of one pending request.
   *
   * @param size pending payload 长度。
   * @param size Pending payload length.
   * @param info pending 请求元数据。
   * @param info Pending request metadata.
   */
  void LoadPending(size_t size, const WriteInfoBlock& info)
  {
    pending_length_ = size;
    pending_info_ = info;
    pending_valid_.Set();
  }

  /**
   * @brief 将 pending 请求提升为 active 请求。
   * @brief Promote the pending request into the active slot.
   *
   * 若当前没有有效 pending 请求，则返回 false。
   * Returns false when no valid pending request is present.
   *
   * @return 提升成功返回 true，否则返回 false。
   * @return True on successful promotion, otherwise false.
   */
  bool PromotePending()
  {
    if (!pending_valid_.TestAndClear())
    {
      return false;
    }

    bytes_.FlipActiveBlock();
    active_length_ = pending_length_;
    active_offset_ = 0U;
    active_info_ = pending_info_;
    active_valid_ = true;
    pending_length_ = 0U;
    pending_info_ = {};
    return true;
  }

  /**
   * @brief 清除当前 active 请求状态。
   * @brief Clear the current active request state.
   */
  void ClearActive()
  {
    active_length_ = 0U;
    active_offset_ = 0U;
    active_info_ = {};
    active_valid_ = false;
  }

  /**
   * @brief 清除当前 pending 请求状态。
   * @brief Clear the current pending request state.
   */
  void ClearPending()
  {
    pending_valid_.Clear();
    pending_length_ = 0U;
    pending_info_ = {};
  }

 private:
  DoubleBuffer bytes_{};          ///< payload 双缓冲对象。 Payload double-buffer object.
  size_t active_length_ = 0U;    ///< active payload 长度。 Active payload length.
  size_t pending_length_ = 0U;   ///< pending payload 长度。 Pending payload length.
  size_t active_offset_ = 0U;    ///< active payload 已推进偏移。 Active progressed offset.
  WriteInfoBlock active_info_ = {};   ///< active 请求元数据。 Active request metadata.
  WriteInfoBlock pending_info_ = {};  ///< pending 请求元数据。 Pending request metadata.
  bool active_valid_ = false;         ///< active 请求是否有效。 Whether an active request is valid.
  Flag::Atomic pending_valid_{};      ///< pending 请求是否有效。 Whether a pending request is valid.
};

}  // namespace LibXR
