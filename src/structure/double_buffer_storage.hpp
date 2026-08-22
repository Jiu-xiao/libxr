#pragma once

#include <cstddef>
#include <cstdint>

#include "libxr_assert.hpp"
#include "libxr_type.hpp"

namespace LibXR
{

/**
 * @brief 供 active/pending 状态机使用的纯存储双 block 视图 / Storage-only two-block
 * view for active/pending state machines
 *
 * 本类只持有两个 backing block 指针、单 block 容量和 active 索引，不记录 pending
 * 有效性或任一 block 的数据长度；这些语义由外围状态机持有。 / This class owns only the
 * two block pointers, block size, and active index. Pending validity and data lengths
 * belong to the surrounding state machine.
 */
class DoubleBufferStorage
{
 public:
  /** @brief 构造未初始化的存储视图 / Construct an uninitialized storage view. */
  DoubleBufferStorage() = default;

  /**
   * @brief 在连续的双 block 存储上构造视图 / Construct over contiguous two-block
   * storage
   * @param raw_data 对半分为两个等长 block 的连续存储；空视图可使用 `nullptr + 0` /
   * Contiguous storage split into two equal blocks; `nullptr + 0` creates an empty view
   */
  explicit DoubleBufferStorage(const RawData& raw_data) { Init(raw_data); }

  /**
   * @brief 绑定连续存储并将 active 索引重置为 block 0 / Bind storage and reset active
   * to block 0
   * @param raw_data 对半分为两个等长 block 的连续存储 / Contiguous storage split into
   * two equal blocks
   */
  void Init(const RawData& raw_data);

  /** @brief 保留存储绑定并将 active 索引重置为 block 0 / Reset active to block 0. */
  void Reset();

  /** @return 当前 active block 地址 / Current active block address. */
  [[nodiscard]] uint8_t* ActiveBuffer() const;

  /** @return 当前 inactive block 地址 / Current inactive block address. */
  [[nodiscard]] uint8_t* PendingBuffer() const;

  /**
   * @brief 返回指定 block / Return a selected block
   * @param block block 索引，只允许 0 或 1 / Block index, either 0 or 1
   * @return 对应 block 地址 / Selected block address
   */
  [[nodiscard]] uint8_t* Buffer(int block) const;

  /** @return 每个 block 的字节容量 / Capacity of each block in bytes. */
  [[nodiscard]] size_t Size() const;

  /** @brief 在 block 0 和 block 1 之间翻转 active 索引 / Flip the active index. */
  void FlipActiveBlock() { active_ ^= 1; }

  /** @return 当前 active block 索引 / Current active block index. */
  [[nodiscard]] int ActiveBlock() const { return active_; }

  /**
   * @brief 选择 active block / Select the active block
   * @param block true 选择 block 1，false 选择 block 0 / True selects block 1; false
   * selects block 0
   */
  void SetActiveBlock(bool block) { active_ = block ? 1 : 0; }

 private:
  uint8_t* buffer_[2] = {nullptr, nullptr};
  size_t size_ = 0U;
  int active_ = 0;
};

}  // namespace LibXR
