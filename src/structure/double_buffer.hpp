#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libxr_type.hpp"  // RawData

namespace LibXR
{ /**
   * @class DoubleBuffer
   * @brief 双缓冲区管理类 / Double buffer manager class
   *
   * 该类用于在嵌入式场景中管理双缓冲传输结构，支持主动缓冲、备用缓冲切换与填充。
   * 用于实现无缝 DMA 或 USB 数据流水线发送，提高吞吐效率。
   * This class provides double-buffer control for efficient pipelined transmission
   * such as USB or DMA streaming.
   */
class DoubleBuffer
{
 public:
  /**
   * @brief 构造函数，使用连续内存构造两个缓冲区
   *        Constructs a double buffer using one continuous memory block
   *
   * @param raw_data 连续内存区，大小必须为两个缓冲区之和 / The raw memory to be split
   */
  explicit DoubleBuffer(const LibXR::RawData& raw_data);

  /**
   * @brief 获取当前正在使用的缓冲区指针
   *        Returns the currently active buffer
   *
   * @return 指向活动缓冲区的指针 / Pointer to the active buffer
   */
  uint8_t* ActiveBuffer() const;

  /**
   * @brief 获取备用缓冲区的指针
   *        Returns the pending (inactive) buffer
   *
   * @return 指向备用缓冲区的指针 / Pointer to the pending buffer
   */
  uint8_t* PendingBuffer() const;

  /**
   * @brief 获取每个缓冲区的大小（单位：字节）
   *        Gets the size of each buffer in bytes
   *
   * @return 缓冲区大小 / Size of each buffer
   */
  size_t Size() const;

  /**
   * @brief 切换到备用缓冲区（若其有效）
   *        Switches to the pending buffer if it's valid
   *
   * @note 如果 pending 缓冲区未准备好，则不执行切换。
   *       No switch is performed if no pending data is ready.
   */
  void Switch();

  /**
   * @brief 判断是否有待切换的缓冲区
   *        Checks whether a pending buffer is ready
   *
   * @return 若有数据待切换返回 true，否则返回 false / True if pending buffer is valid
   */
  bool HasPending() const;

  /**
   * @brief 向备用缓冲区写入数据（不可重入）
   *        Fills the pending buffer with data (non-reentrant)
   *
   * @param data 数据源指针 / Pointer to the source data
   * @param len 数据长度（字节） / Data length in bytes
   * @return 写入成功返回 true，失败（如已占用或溢出）返回 false
   *         Returns true on success, false if buffer already pending or overflow
   */
  bool FillPending(const uint8_t* data, size_t len);

  /**
   * @brief 向当前使用的缓冲区直接写入数据
   *        Fills the active buffer directly
   *
   * @param data 数据源指针 / Source data pointer
   * @param len 数据长度 / Length to write
   * @return 成功返回 true，失败（超出大小）返回 false
   *         True on success, false if length exceeds buffer size
   */
  bool FillActive(const uint8_t* data, size_t len);

  /**
   * @brief 手动启用 pending 状态
   *        Manually sets the pending state to true
   *
   * 通常与 FillActive() 配合使用，表示下次发送使用 FillActive 写入的缓冲。
   */
  void EnablePending();

  /**
   * @brief 获取 pending 缓冲区中准备好的数据长度
   *        Gets the size of valid data in pending buffer
   *
   * @return 准备好的长度 / Valid pending buffer data length
   */
  size_t GetPendingLength() const;

  /**
   * @brief 获取当前活动缓冲区中准备好的数据长度
   *        Gets the size of valid data in active buffer
   *
   * @return 准备好的长度 / Valid active buffer data length
   */
  size_t GetActiveLength() const { return active_len_; }

  /**
   * @brief 设置备用缓冲区的数据长度
   *        Sets the size of the pending buffer
   *
   * @param length 数据长度（字节） / Data length in bytes
   */
  void SetPendingLength(size_t length) { pending_len_ = length; }

  /**
   * @brief 设置当前活动缓冲区的数据长度
   *        Sets the size of the active buffer
   *
   * @param length 数据长度（字节） / Data length in bytes
   */
  void SetActiveLength(size_t length) { active_len_ = length; }

  /**
   * @brief 设置当前活动缓冲区
   *        Sets the active buffer
   *
   * @param block true 表示使用第二个缓冲区，false 表示使用第一个缓冲区
   */
  void SetActiveBlock(bool block) { active_ = block ? 1 : 0; }

 private:
  uint8_t* buffer_[2];  ///< 双缓冲区指针 / Double buffer pointers
  const size_t SIZE;    ///< 单个缓冲区大小 / Size of each buffer
  int active_ = 0;      ///< 当前活动缓冲区编号 / Index of active buffer
  bool pending_valid_ =
      false;                ///< 标记备用区是否准备好 / Whether pending buffer is ready
  size_t active_len_ = 0;   ///< 当前活动缓冲区有效数据长度 / Length of active data
  size_t pending_len_ = 0;  ///< 备用缓冲区有效数据长度 / Length of pending data
};
}  // namespace LibXR
