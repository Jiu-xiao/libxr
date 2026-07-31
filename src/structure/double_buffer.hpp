#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "double_buffer_storage.hpp"

namespace LibXR
{

/**
 * @brief 完整的 active/pending 双缓冲管理器 / Full active/pending double-buffer manager
 *
 * 本类持有存储视图以及通用 pending/长度记录，供 USB、SPI、ESP helper 和独立结构测试
 * 使用。提交或失败语义不同的状态机应直接使用 `DoubleBufferStorage`。 / This class owns
 * the storage view and generic pending/length bookkeeping used by USB, SPI, ESP helpers,
 * and structure tests. State machines with different commit or failure semantics should
 * use `DoubleBufferStorage` directly.
 */
class DoubleBuffer
{
 public:
  /**
   * @brief 构造未初始化的双缓冲管理器 / Construct an uninitialized manager
   */
  DoubleBuffer() = default;

  /**
   * @brief 在连续的双 block 存储上构造管理器 / Construct over one contiguous two-block
   * storage region
   * @param raw_data 连续存储；空缓冲可使用 `nullptr + 0` / Contiguous storage; an empty
   * buffer may use `nullptr + 0`
   */
  explicit DoubleBuffer(const RawData& raw_data);

  /**
   * @brief 绑定连续 backing storage 并重置 active/pending 状态 / Bind backing storage
   * and reset active/pending state
   * @param raw_data 对半分为两个等长 block 的连续存储 / Contiguous storage split into
   * two equal blocks
   */
  void Init(const RawData& raw_data);

  /**
   * @brief 保留 backing storage 并重置 active/pending 状态 / Reset state while retaining
   * backing storage
   */
  void Reset();

  /** @return 当前 active buffer 地址 / Current active-buffer address. */
  uint8_t* ActiveBuffer() const;

  /** @return 可写 pending 数据的 inactive buffer 地址 / Inactive-buffer address. */
  uint8_t* PendingBuffer() const;

  /**
   * @brief 返回指定 backing block / Return a selected backing block
   * @param block block 索引，只允许 0 或 1 / Block index, either 0 or 1
   * @return 对应 block 地址 / Selected block address
   */
  uint8_t* Buffer(int block) const;

  /** @return 每个 backing block 的字节容量 / Capacity of each block in bytes. */
  size_t Size() const;

  /**
   * @brief pending 有效时切换 active block / Switch to the pending block when valid
   */
  void Switch();

  /** @return pending 数据可切换时为 true / True when pending data is ready. */
  bool HasPending() const;

  /**
   * @brief 将数据复制到 pending block 并标记有效 / Copy data into pending and mark it
   * valid
   * @param data 待复制数据 / Source data
   * @param len 待复制字节数 / Number of bytes to copy
   * @return pending 已有效或长度超出 block 时为 false / False when pending is already
   * valid or the length exceeds the block size
   */
  bool FillPending(const uint8_t* data, size_t len);

  /**
   * @brief 将数据复制到 active block，不修改 pending 状态 / Copy data into active
   * without changing pending state
   * @param data 待复制数据 / Source data
   * @param len 待复制字节数 / Number of bytes to copy
   * @return 长度超出 block 时为 false / False when the length exceeds the block size
   */
  bool FillActive(const uint8_t* data, size_t len);

  /**
   * @brief 将手工填充的 pending 数据标记为有效 / Mark manually populated pending data
   * valid
   */
  void EnablePending();

  /** @return pending 有效长度；无 pending 时为零 / Valid pending length, or zero. */
  size_t GetPendingLength() const;

  /** @return active block 的有效长度 / Valid length of the active block. */
  size_t GetActiveLength() const { return active_len_; }

  /**
   * @brief 设置 pending 数据长度 / Set pending data length
   * @param length 有效字节数 / Valid byte count
   */
  void SetPendingLength(size_t length) { pending_len_ = length; }

  /**
   * @brief 设置 active 数据长度 / Set active data length
   * @param length 有效字节数 / Valid byte count
   */
  void SetActiveLength(size_t length) { active_len_ = length; }

  /**
   * @brief 翻转 active block 索引，不修改长度记录 / Flip the active block index without
   * changing lengths
   */
  void FlipActiveBlock() { storage_.FlipActiveBlock(); }

  /** @return 当前 active block 索引 / Current active block index. */
  int ActiveBlock() const { return storage_.ActiveBlock(); }

  /**
   * @brief 选择 active block / Select the active block
   * @param block true 选择 block 1，false 选择 block 0 / True selects block 1; false
   * selects block 0
   */
  void SetActiveBlock(bool block) { storage_.SetActiveBlock(block); }

 private:
  DoubleBufferStorage storage_{};
  bool pending_valid_ = false;
  size_t active_len_ = 0U;
  size_t pending_len_ = 0U;
};

}  // namespace LibXR
