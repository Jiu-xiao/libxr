#pragma once

#include <cstdint>

#include "libxr_rw.hpp"
#include "libxr_type.hpp"

namespace LibXR::USB
{

/**
 * @brief CDC UART OUT 端点背压状态辅助器。
 * @brief Backpressure-state helper for the CDC UART OUT endpoint.
 *
 * 当 RX software queue 空间不足时，OUT completion 中收到的 USB buffer 不能立刻
 * rearm，需要先把本次 packet 暂存为 pending，等上层读走队列数据后再回填。
 * When the RX software queue has no room, the packet reported by OUT completion cannot
 * rearm immediately. It is held as pending until upper-layer reads free queue space.
 */
class CDCUartRxBackpressureHelper final
{
 public:
  /**
   * @brief 构造背压辅助器。
   * @brief Construct the backpressure helper.
   * @param port CDC UART RX 读端口。 CDC UART RX read port.
   */
  explicit CDCUartRxBackpressureHelper(ReadPort& port) : port_(port) {}

  /**
   * @brief 重置背压状态。
   * @brief Reset the backpressure state.
   */
  void Reset()
  {
    paused_ = false;
    pending_data_ = {nullptr, 0};
  }

  /**
   * @brief 判断 OUT 接收是否处于背压暂停状态。
   * @brief Check whether OUT receiving is paused by backpressure.
   */
  bool Paused() const { return paused_; }

  /**
   * @brief 将 OUT packet 写入 RX queue，失败时进入背压暂停状态。
   * @brief Push an OUT packet into the RX queue or pause on backpressure.
   * @param data OUT completion 提供的数据。 Data reported by OUT completion.
   * @param in_isr 是否在 ISR 上下文。 Whether the call runs in ISR context.
   * @return true 表示 packet 已进入 RX queue 或为空；false 表示已进入背压暂停。
   * @return true when the packet is queued or empty; false when backpressure is active.
   */
  bool PushOrPause(ConstRawData& data, bool in_isr)
  {
    if (data.size_ == 0)
    {
      return true;
    }

    auto push_ans = port_.queue_data_->PushBatch(
        reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
    if (push_ans == ErrorCode::OK)
    {
      port_.ProcessPendingReads(in_isr);
      return true;
    }

    paused_ = true;
    pending_data_ = data;
    return false;
  }

  /**
   * @brief 尝试把 pending packet 回填进 RX queue。
   * @brief Try to push the pending packet back into the RX queue.
   * @param in_isr 是否在 ISR 上下文。 Whether the call runs in ISR context.
   * @return true 表示没有 pending 或回填成功；false 表示队列空间仍不足。
   * @return true when there is no pending data or refill succeeds; false on no space.
   */
  bool FlushPending(bool in_isr)
  {
    if (!paused_)
    {
      return true;
    }

    if (pending_data_.size_ == 0)
    {
      return true;
    }

    if (port_.queue_data_->EmptySize() < pending_data_.size_)
    {
      return false;
    }

    auto ans = port_.queue_data_->PushBatch(
        reinterpret_cast<const uint8_t*>(pending_data_.addr_), pending_data_.size_);
    if (ans != ErrorCode::OK)
    {
      return false;
    }

    pending_data_ = {nullptr, 0};
    port_.ProcessPendingReads(in_isr);
    return true;
  }

  /**
   * @brief 标记 OUT endpoint 已重新 armed。
   * @brief Mark the OUT endpoint as rearmed.
   */
  void MarkRearmed() { paused_ = false; }

 private:
  ///< CDC UART RX 读端口。 CDC UART RX read port.
  ReadPort& port_;
  ///< OUT 是否因 RX queue 空间不足而暂停。 OUT paused by RX queue backpressure.
  bool paused_ = false;
  ///< 暂存的 OUT packet。 Pending OUT packet.
  ConstRawData pending_data_{nullptr, 0};
};

}  // namespace LibXR::USB
