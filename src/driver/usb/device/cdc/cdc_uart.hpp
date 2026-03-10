#pragma once

#include "cdc_base.hpp"
#include "ep.hpp"
#include "flag.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{
/**
 * @brief WritePort（info 队列 + data 队列）的“单 op 不跨界”出队辅助器
 *        Dequeue helper for WritePort (info + data) without crossing op boundary
 */
class CDCUartTxOpDequeueHelper final
{
 public:
  /**
   * @brief 构造函数
   *        Constructor
   *
   * @param port 写端口引用 / Write port reference
   */
  explicit CDCUartTxOpDequeueHelper(WritePort& port) : port_(port) {}

  /**
   * @brief 重置内部状态（head 缓存与偏移）
   *        Reset internal state (cached head and offset)
   */
  void Reset()
  {
    head_valid_ = false;
    offset_ = 0;
  }

  /**
   * @brief 是否存在可处理的 op
   *        Whether any op exists
   *
   * @return true 存在缓存 head 或 info 队列非空 / Cached head exists or info queue
   * non-empty
   * @return false 否则 / Otherwise
   */
  bool HasOp() { return head_valid_ || (port_.queue_info_->Size() > 0); }

  /**
   * @brief 从 data 队列搬运数据到目标 buffer，并推进 offset（不 pop info）
   *        Dequeue bytes from data queue into destination buffer and advance offset (no
   * info pop)
   *
   * @param dst     目标 buffer / Destination buffer
   * @param cap     目标 buffer 容量（字节）/ Destination capacity (bytes)
   * @param out_len 实际搬运长度（字节）/ Bytes moved
   * @return ErrorCode::PENDING / ErrorCode::OK / 其它错误码 / Other error codes
   */
  ErrorCode Take(uint8_t* dst, std::size_t cap, std::size_t& out_len)
  {
    auto ec = EnsureHead();
    if (ec != ErrorCode::OK)
    {
      out_len = 0;
      return ec;  // EMPTY / FAILED
    }

    const std::size_t REMAINING = Remaining();
    if (REMAINING == 0)
    {
      out_len = 0;
      return ErrorCode::FAILED;
    }

    const std::size_t TAKE = (REMAINING < cap) ? REMAINING : cap;

    if (port_.queue_data_->PopBatch(dst, TAKE) != ErrorCode::OK)
    {
      out_len = 0;
      return ErrorCode::FAILED;
    }

    offset_ += TAKE;
    out_len = TAKE;

    return (offset_ == head_.data.size_) ? ErrorCode::OK : ErrorCode::PENDING;
  }

  /**
   * @brief head op 是否已全部出队
   *        Whether the cached head op is fully dequeued
   *
   * @return true 已完成 / Completed
   * @return false 未完成 / Not completed
   */
  bool HeadCompleted() const { return head_valid_ && (offset_ == head_.data.size_); }

  /**
   * @brief 在 head 完成后 pop info 并重置状态
   *        Pop info after head completes and reset state
   *
   * @param completed_info 可选输出：被 pop 的 info / Optional output: popped info
   * @return ErrorCode::OK 成功 / Success
   * @return ErrorCode::FAILED head 未完成 / Head not completed
   */
  ErrorCode PopCompleted(WriteInfoBlock* completed_info = nullptr)
  {
    if (!HeadCompleted())
    {
      return ErrorCode::FAILED;
    }

    WriteInfoBlock popped{};
    auto ans = port_.queue_info_->Pop(popped);
    ASSERT(ans == ErrorCode::OK);

    if (completed_info)
    {
      *completed_info = popped;
    }

    Reset();
    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 确保 head 缓存可用（必要时 Peek info）
   *        Ensure cached head is valid (Peek info if needed)
   *
   * @return ErrorCode::OK 成功 / Success
   * @return ErrorCode::EMPTY info 队列为空 / Info queue empty
   */
  ErrorCode EnsureHead()
  {
    if (head_valid_)
    {
      return ErrorCode::OK;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    head_ = info;
    head_valid_ = true;
    offset_ = 0;
    return ErrorCode::OK;
  }

  /**
   * @brief 当前 op 剩余未出队字节数
   *        Remaining bytes of current op
   *
   * @return 剩余字节数 / Remaining bytes
   */
  std::size_t Remaining() const
  {
    ASSERT(head_valid_);
    ASSERT(head_.data.size_ >= offset_);
    return head_.data.size_ - offset_;
  }

 private:
  WritePort& port_;          ///< 写端口引用 / Write port reference
  bool head_valid_ = false;  ///< head 缓存有效标志 / Cached head valid flag
  WriteInfoBlock head_{};    ///< 缓存的 head info / Cached head info
  std::size_t offset_ = 0;   ///< 当前 op 已出队偏移 / Dequeued offset within current op
};

}  // namespace LibXR

namespace LibXR::USB
{

class CDCUart;

/**
 * @brief CDC UART 读端口（背压 + pending 缓存）
 *        CDC UART read port (backpressure + pending cache)
 */
class CDCUartReadPort : public ReadPort
{
 public:
  /**
   * @brief 构造函数
   *        Constructor
   *
   * @param size  RX 缓冲区大小 / RX buffer size
   * @param owner 所属 CDCUart 实例 / Owning CDCUart instance
   */
  explicit CDCUartReadPort(uint32_t size, CDCUart& owner) : ReadPort(size), owner_(owner)
  {
  }

  /**
   * @brief 数据队列被消费时回调（解除背压并尝试恢复 OUT rearm）
   *        Called when RX queue is dequeued (lift backpressure and try to rearm OUT)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   */
  void OnRxDequeue(bool in_isr) override;

  CDCUartReadPort& operator=(ReadFun fun)
  {
    ReadPort::operator=(fun);
    return *this;
  }

  CDCUart& owner_;  ///< 所属 CDCUart / Owning CDCUart

  bool recv_pause_ =
      false;  ///< 背压标志：true 表示 OUT 未 rearm / Backpressure flag: OUT not rearmed
  ConstRawData pending_data_{
      nullptr,
      0};  ///< pending 数据（指向底层 USB buffer）/ Pending data pointing to USB buffer
};

/**
 * @brief USB CDC-ACM UART 适配器
 *        USB CDC-ACM UART adapter
 */
class CDCUart : public CDCBase, public LibXR::UART
{
 public:
  using LibXR::UART::Read;
  using LibXR::UART::read_port_;
  using LibXR::UART::Write;
  using LibXR::UART::write_port_;

  /**
   * @brief 构造函数
   *        Constructor
   *
   * @param rx_buffer_size RX 缓冲区大小 / RX buffer size
   * @param tx_buffer_size TX 端点缓冲区大小 / TX endpoint buffer size
   * @param tx_queue_size  TX info 队列深度 / TX info queue depth
   * @param data_in_ep_num  Data IN 端点号 / Data IN EP number
   * @param data_out_ep_num Data OUT 端点号 / Data OUT EP number
   * @param comm_ep_num     通信端点号 / Comm EP number
   */
  CDCUart(size_t rx_buffer_size = 128, size_t tx_buffer_size = 128,
          size_t tx_queue_size = 5,
          Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
          Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
          Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num),
        LibXR::UART(&read_port_cdc_, &write_port_cdc_),
        read_port_cdc_(rx_buffer_size, *this),
        write_port_cdc_(tx_queue_size, tx_buffer_size),
        tx_deq_(write_port_cdc_)
  {
    read_port_cdc_ = ReadFun;    // NOLINT
    write_port_cdc_ = WriteFun;  // NOLINT
  }

  /**
   * @brief 设置 UART 配置（CDC Line Coding）
   *        Set UART configuration (CDC Line Coding)
   *
   * @param cfg UART 配置 / UART configuration
   * @return 错误码 / Error code
   */
  ErrorCode SetConfig(UART::Configuration cfg) override
  {
    auto& line_coding = GetLineCoding();

    switch (cfg.stop_bits)
    {
      case 1:
        line_coding.bCharFormat = 0;
        break;
      case 2:
        line_coding.bCharFormat = 2;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    switch (cfg.parity)
    {
      case UART::Parity::NO_PARITY:
        line_coding.bParityType = 0;
        break;
      case UART::Parity::ODD:
        line_coding.bParityType = 1;
        break;
      case UART::Parity::EVEN:
        line_coding.bParityType = 2;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    switch (cfg.data_bits)
    {
      case 5:
      case 6:
      case 7:
      case 8:
      case 16:
        line_coding.bDataBits = static_cast<uint8_t>(cfg.data_bits);
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    line_coding.dwDTERate = cfg.baudrate;
    SendSerialState();
    return ErrorCode::OK;
  }

  /**
   * @brief 尝试 rearm OUT（背压恢复/持续接收）
   *        Try to rearm OUT endpoint (backpressure recovery / continuous RX)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @return true 成功 rearm / Rearmed successfully
   * @return false 未 rearm（忙/空间不足/端点不可用）/ Not rearmed (busy / insufficient
   * space / endpoint unavailable)
   */
  bool TryRearmOut(bool in_isr)
  {
    auto ep_data_out = GetDataOutEndpoint();
    if (ep_data_out == nullptr)
    {
      return false;
    }

    const std::size_t MPS = ep_data_out->MaxPacketSize();
    if (MPS == 0 || read_port_cdc_.queue_data_ == nullptr)
    {
      return false;
    }

    if (read_port_cdc_.recv_pause_)
    {
      if (read_port_cdc_.queue_data_->EmptySize() >= read_port_cdc_.pending_data_.size_)
      {
        auto push_ans = read_port_cdc_.queue_data_->PushBatch(
            reinterpret_cast<const uint8_t*>(read_port_cdc_.pending_data_.addr_),
            read_port_cdc_.pending_data_.size_);
        if (push_ans == ErrorCode::OK)
        {
          read_port_cdc_.ProcessPendingReads(in_isr);
        }
        else
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }

    if (ep_data_out->GetState() == Endpoint::State::BUSY)
    {
      return false;
    }

    auto ans = ep_data_out->Transfer(MPS);
    if (ans == ErrorCode::OK)
    {
      read_port_cdc_.recv_pause_ = false;
      return true;
    }

    return false;
  }

 protected:
  /**
   * @brief 解绑端点（清理队列、状态与背压标志）
   *        Unbind endpoints (cleanup queues, states, and backpressure flags)
   *
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param in_isr        是否在 ISR 上下文 / In ISR context
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr) override
  {
    CDCBase::UnbindEndpoints(endpoint_pool, in_isr);

    WriteInfoBlock info{};

    write_port_cdc_.queue_data_->Reset();
    tx_deq_.Reset();

    while (write_port_cdc_.queue_info_->Pop(info) == ErrorCode::OK)
    {
      write_port_cdc_.Finish(in_isr, ErrorCode::INIT_ERR, info);
    }

    need_write_zlp_ = false;

    read_port_cdc_.recv_pause_ = false;
    read_port_cdc_.pending_data_ = {nullptr, 0};

    write_port_cdc_.Reset();
  }

  /**
   * @brief 写端口回调（TX）
   *        Write port callback (TX)
   *
   * @details
   * - 允许在一次调用内对同一个 op 触发多次 Transfer（每次预写后检查是否可立即发送）
   *   Allows multiple Transfer kicks for the same op within one call (check-send after
   * each prefill)
   * - 仅当启动该 op 最后一段 Transfer 后返回非 PENDING
   *   Return non-PENDING only after the last segment Transfer of the op is kicked
   * - 预写仅执行 Take + SetActiveLength，不调用 Finish
   *   Prefill performs Take + SetActiveLength only; Finish is not called here
   *
   * @param port  写端口 / Write port
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @return 错误码 / Error code
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr)
  {
    UNUSED(in_isr);

    CDCUart* cdc = CONTAINER_OF(&port, CDCUart, write_port_cdc_);

    /**
     * @note
     * 不在 IN ISR；否则由 IN ISR 处理。
     * Not in IN ISR; otherwise handled by IN ISR.
     */
    if (cdc->in_write_isr_.IsSet())
    {
      return ErrorCode::PENDING;
    }

    auto ep = cdc->GetDataInEndpoint();
    if (ep == nullptr)
    {
      return ErrorCode::FAILED;
    }

    if (!cdc->Inited())
    {
      WriteInfoBlock info{};
      auto pop_ans = port.queue_info_->Pop(info);
      if (pop_ans != ErrorCode::OK)
      {
        return ErrorCode::EMPTY;
      }

      auto drop_ans = port.queue_data_->PopBatch(nullptr, info.data.size_);
      UNUSED(drop_ans);
      ASSERT(drop_ans == ErrorCode::OK);

      return ErrorCode::INIT_ERR;  // 非 PENDING -> 上层 finish 一次 / Non-PENDING
                                   // triggers one finish upstream
    }

    /**
     * @note
     * 入口条件：ActiveLength==0 。
     * Entry condition: ActiveLength==0.
     */
    if (ep->GetActiveLength() != 0)
    {
      return ErrorCode::PENDING;
    }

    /**
     * @note
     * 若出现新数据，则取消 ZLP。
     * Cancel pending ZLP if new data becomes available.
     */
    if (cdc->tx_deq_.HasOp())
    {
      cdc->need_write_zlp_ = false;
    }

    /**
     * @brief 当前 ActiveLength 槽对应数据段的完成态
     *        Completion state associated with the current ActiveLength slot
     */
    ErrorCode slot_ec = ErrorCode::PENDING;

    // 预写第一段 / Prefill first segment
    {
      auto buffer = ep->GetBuffer();
      std::size_t len = 0;

      slot_ec =
          cdc->tx_deq_.Take(reinterpret_cast<uint8_t*>(buffer.addr_), buffer.size_, len);
      if (slot_ec == ErrorCode::EMPTY || len == 0)
      {
        return ErrorCode::PENDING;
      }
      if (slot_ec != ErrorCode::OK && slot_ec != ErrorCode::PENDING)
      {
        return slot_ec;
      }

      ep->SetActiveLength(len);
    }

    std::atomic_signal_fence(std::memory_order_seq_cst);

    // 循环：可立即发送则发送；发送后预写下一段并继续检查 / Loop: send if possible; then
    // prefill next segment
    while (true)
    {
      const std::size_t TO_SEND = ep->GetActiveLength();

      /**
       * @note
       * 不可发送条件 / Not-sendable conditions:
       * - 端点非 IDLE / Endpoint not IDLE
       * - ActiveLength==0（槽已被清零或未发布）/ ActiveLength==0 (slot cleared or not
       * published)
       * - 当前时刻无可处理 op（避免预写段在并发路径被消费后继续推进）/ No op available at
       * this moment
       */
      if (ep->GetState() != Endpoint::State::IDLE || TO_SEND == 0 ||
          !cdc->tx_deq_.HasOp())
      {
        return ErrorCode::PENDING;
      }

      std::atomic_signal_fence(std::memory_order_seq_cst);

      // 启动一次 Transfer / Kick one Transfer
      ep->SetActiveLength(0);
      auto ans = ep->Transfer(TO_SEND);
      ASSERT(ans == ErrorCode::OK);

      /**
       * @note
       * 若本次启动的是该 op 最后一段：启动后 pop，并返回 OK 触发 finish。
       * If this kicked segment is the last of the op: pop after kick and return OK to
       * trigger finish.
       */
      if (slot_ec == ErrorCode::OK && cdc->tx_deq_.HeadCompleted())
      {
        auto pop_ok = cdc->tx_deq_.PopCompleted(nullptr);
        ASSERT(pop_ok == ErrorCode::OK);

        // ZLP 判定 / ZLP decision
        const std::size_t MPS = ep->MaxPacketSize();
        if (MPS > 0 && (TO_SEND % MPS) == 0 && ep->GetActiveLength() == 0 &&
            !cdc->tx_deq_.HasOp())
        {
          cdc->need_write_zlp_ = true;
        }

        return ErrorCode::OK;  // 非 PENDING -> 上层 finish 一次 / Non-PENDING triggers
                               // one finish upstream
      }

      // 预写下一段 / Prefill next segment
      if (!cdc->tx_deq_.HasOp())
      {
        return ErrorCode::PENDING;
      }

      auto buffer = ep->GetBuffer();
      std::size_t len2 = 0;

      slot_ec =
          cdc->tx_deq_.Take(reinterpret_cast<uint8_t*>(buffer.addr_), buffer.size_, len2);
      if (slot_ec == ErrorCode::EMPTY || len2 == 0)
      {
        return ErrorCode::PENDING;
      }
      if (slot_ec != ErrorCode::OK && slot_ec != ErrorCode::PENDING)
      {
        return slot_ec;
      }

      ep->SetActiveLength(len2);
      // 下一轮继续检查是否可立即发送 / Next iteration checks sendability again
    }
  }

  static ErrorCode ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

  /**
   * @brief OUT 完成回调（RX）
   *        OUT complete callback (RX)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @param data   OUT 接收数据 / Received OUT data
   */
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    if (data.size_ > 0)
    {
      auto push_ans = read_port_cdc_.queue_data_->PushBatch(
          reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
      if (push_ans == ErrorCode::OK)
      {
        read_port_cdc_.ProcessPendingReads(in_isr);
      }
      else
      {
        read_port_cdc_.recv_pause_ = true;
        read_port_cdc_.pending_data_ = data;
        return;
      }
    }

    (void)TryRearmOut(in_isr);
  }

  /**
   * @brief IN 完成回调（TX）
   *        IN complete callback (TX)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @param data   IN 数据（未使用）/ IN data (unused)
   */
  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(data);

    Flag::ScopedRestore isr_flag(in_write_isr_);

    auto ep = GetDataInEndpoint();
    if (ep == nullptr)
    {
      return;
    }

    if (!Inited())
    {
      WriteInfoBlock info{};
      tx_deq_.Reset();

      while (write_port_cdc_.queue_info_->Pop(info) == ErrorCode::OK)
      {
        write_port_cdc_.queue_data_->Reset();
        ASSERT(write_port_cdc_.queue_data_->Size() == 0);
        write_port_cdc_.Finish(in_isr, ErrorCode::INIT_ERR, info);
      }
      return;
    }

    // ZLP：仅在此刻跨-op 无数据时发送 / ZLP: send only if no data across ops at this
    // moment
    if (need_write_zlp_)
    {
      if (ep->GetActiveLength() == 0 && !tx_deq_.HasOp())
      {
        auto z = ep->TransferZLP();
        ASSERT(z == ErrorCode::OK);
        need_write_zlp_ = false;
        return;
      }
      need_write_zlp_ = false;
    }

    // ActiveLength==0 时不读取队列 / Do not read queues when ActiveLength==0
    const std::size_t PENDING_LEN = ep->GetActiveLength();
    if (PENDING_LEN == 0)
    {
      return;
    }

    // 1) 续发：本 ISR 仅启动一次 Transfer / Continue: only one Transfer is kicked in this
    // ISR
    ep->SetActiveLength(0);
    auto ans = ep->Transfer(PENDING_LEN);
    ASSERT(ans == ErrorCode::OK);

    // 2) 若为 head op 最后一段：启动后 pop+Finish（一次）/ If last segment: pop+Finish
    // once
    if (tx_deq_.HeadCompleted())
    {
      WriteInfoBlock completed{};
      auto pop_ok = tx_deq_.PopCompleted(&completed);
      ASSERT(pop_ok == ErrorCode::OK);
      write_port_cdc_.Finish(in_isr, ErrorCode::OK, completed);
    }

    // 3) 预写：仅在已启动 Transfer 后允许读取队列 / Prefill: allowed only after kicking
    // Transfer
    bool primed = false;
    if (tx_deq_.HasOp())
    {
      auto buffer = ep->GetBuffer();
      std::size_t len2 = 0;

      auto ec2 =
          tx_deq_.Take(reinterpret_cast<uint8_t*>(buffer.addr_), buffer.size_, len2);
      if ((ec2 == ErrorCode::OK || ec2 == ErrorCode::PENDING) && len2 > 0)
      {
        ep->SetActiveLength(len2);
        primed = true;
      }
    }

    // 4) ZLP 判定 / ZLP decision
    const std::size_t MPS = ep->MaxPacketSize();
    if (!primed && PENDING_LEN > 0 && MPS > 0 && (PENDING_LEN % MPS) == 0 &&
        ep->GetActiveLength() == 0 && !tx_deq_.HasOp())
    {
      need_write_zlp_ = true;
    }
  }

 private:
  CDCUartReadPort read_port_cdc_;    ///< CDC RX 读端口 / CDC RX read port
  LibXR::WritePort write_port_cdc_;  ///< CDC TX 写端口 / CDC TX write port

  LibXR::CDCUartTxOpDequeueHelper tx_deq_;  ///< TX 出队辅助器 / TX dequeue helper

  Flag::Plain in_write_isr_;  ///< 写 ISR 保护标志 / Write ISR guard flag

  bool need_write_zlp_{false};  ///< ZLP 需求标志 / ZLP required flag
};

inline void CDCUartReadPort::OnRxDequeue(bool in_isr)
{
  if (!recv_pause_)
  {
    return;
  }

  // pending_data_ 回填 / Push pending_data_ back into queue
  if (pending_data_.size_ > 0)
  {
    if (queue_data_->EmptySize() >= pending_data_.size_)
    {
      auto ans = queue_data_->PushBatch(
          reinterpret_cast<const uint8_t*>(pending_data_.addr_), pending_data_.size_);
      if (ans == ErrorCode::OK)
      {
        pending_data_ = {nullptr, 0};
        ProcessPendingReads(in_isr);
      }
      else
      {
        return;
      }
    }
    else
    {
      return;
    }
  }

  // 尝试恢复 rearm / Try to rearm OUT
  (void)owner_.TryRearmOut(in_isr);
}

}  // namespace LibXR::USB
