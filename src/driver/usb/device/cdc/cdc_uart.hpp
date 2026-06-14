#pragma once

#include "cdc_base.hpp"
#include "cdc_uart_rx_backpressure.hpp"
#include "cdc_uart_tx_dequeue.hpp"
#include "cdc_uart_tx_state.hpp"
#include "ep.hpp"
#include "flag.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"

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
  explicit CDCUartReadPort(uint32_t size, CDCUart& owner)
      : ReadPort(size), owner_(owner), rx_backpressure_(*this)
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
  CDCUartRxBackpressureHelper
      rx_backpressure_;  ///< RX 背压辅助器 / RX backpressure helper
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
  CDCUart(
      size_t rx_buffer_size = 128, size_t tx_buffer_size = 128, size_t tx_queue_size = 5,
      Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO,
      const char* control_interface_string = CDCBase::DEFAULT_CONTROL_INTERFACE_STRING,
      const char* data_interface_string = CDCBase::DEFAULT_DATA_INTERFACE_STRING)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num, control_interface_string,
                data_interface_string),
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

    if (read_port_cdc_.rx_backpressure_.Paused())
    {
      if (!read_port_cdc_.rx_backpressure_.FlushPending(in_isr))
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
      read_port_cdc_.rx_backpressure_.MarkRearmed();
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
    tx_deq_.Reset();
    write_port_cdc_.FailAndClearAll(ErrorCode::INIT_ERR, in_isr);
    read_port_cdc_.FailAndClearAll(ErrorCode::INIT_ERR, in_isr);

    tx_state_.ClearZlp();

    read_port_cdc_.rx_backpressure_.Reset();
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

    auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);

    /**
     * @note
     * 不在 IN ISR；否则由 IN ISR 处理。
     * Not in IN ISR; otherwise handled by IN ISR.
     */
    if (cdc->tx_state_.InCompletion())
    {
      return ErrorCode::PENDING;
    }

    auto ep = cdc->GetDataInEndpoint();
    if (ep == nullptr)
    {
      auto drop_ans = cdc->tx_deq_.DropHead(nullptr);
      if (drop_ans != ErrorCode::OK)
      {
        return drop_ans;
      }
      return ErrorCode::FAILED;
    }

    if (!cdc->Inited())
    {
      auto drop_ans = cdc->tx_deq_.DropHead(nullptr);
      if (drop_ans != ErrorCode::OK)
      {
        return drop_ans;
      }

      return ErrorCode::INIT_ERR;  // 非 PENDING -> 同步上报失败 / Non-PENDING reports
                                   // synchronous failure upstream
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
      cdc->tx_state_.ClearZlp();
    }

    /**
     * @brief 当前 ActiveLength 槽对应数据段的完成态
     *        Completion state associated with the current ActiveLength slot
     */
    ErrorCode slot_ec = ErrorCode::PENDING;

    bool prefilled = false;
    slot_ec = cdc->PrefillTxEndpoint(*ep, prefilled);
    if (!prefilled)
    {
      return ErrorCode::PENDING;
    }
    if (slot_ec != ErrorCode::OK && slot_ec != ErrorCode::PENDING)
    {
      return slot_ec;
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

        // ZLP 判定。
        // ZLP decision.
        const std::size_t MPS = ep->MaxPacketSize();
        if (MPS > 0 && (TO_SEND % MPS) == 0 && ep->GetActiveLength() == 0 &&
            !cdc->tx_deq_.HasOp())
        {
          cdc->tx_state_.RequestZlp();
        }

        return ErrorCode::OK;  // 非 PENDING -> 上层完成一次 / Non-PENDING triggers one
                               // upstream finish
      }

      // 预写下一段。
      // Prefill the next segment.
      if (!cdc->tx_deq_.HasOp())
      {
        return ErrorCode::PENDING;
      }

      slot_ec = cdc->PrefillTxEndpoint(*ep, prefilled);
      if (!prefilled)
      {
        return ErrorCode::PENDING;
      }
      if (slot_ec != ErrorCode::OK && slot_ec != ErrorCode::PENDING)
      {
        return slot_ec;
      }
      // 下一轮继续检查是否可立即发送。
      // The next iteration checks whether sending can continue immediately.
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
    if (!read_port_cdc_.rx_backpressure_.PushOrPause(data, in_isr))
    {
      return;
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

    auto isr_flag = tx_state_.EnterCompletion();

    auto ep = GetDataInEndpoint();
    if (ep == nullptr)
    {
      return;
    }

    if (!Inited())
    {
      tx_deq_.Reset();
      write_port_cdc_.FailAndClearAll(ErrorCode::INIT_ERR, in_isr);
      return;
    }

    if (TrySendPendingZlp(*ep))
    {
      return;
    }

    // ActiveLength==0 时不读取队列。
    // Do not read queues when ActiveLength==0.
    const std::size_t PENDING_LEN = ep->GetActiveLength();
    if (PENDING_LEN == 0)
    {
      return;
    }

    // 续发：本 ISR 仅启动一次 Transfer。
    // Continue: kick only one Transfer in this ISR.
    ep->SetActiveLength(0);
    auto ans = ep->Transfer(PENDING_LEN);
    ASSERT(ans == ErrorCode::OK);

    FinishCompletedTxHead(in_isr);

    // 预写：仅在已启动 Transfer 后允许读取队列。
    // Prefill: queue reads are allowed only after a Transfer has been kicked.
    bool prefilled = false;
    if (tx_deq_.HasOp())
    {
      auto ec2 = PrefillTxEndpoint(*ep, prefilled);
      UNUSED(ec2);
    }

    RequestZlpIfNeeded(*ep, PENDING_LEN, prefilled);
  }

 private:
  /**
   * @brief 若存在待发送 ZLP，则在当前无数据时发送一次。
   * @brief Send one pending ZLP when no data is currently active.
   * @param ep Data IN endpoint。 Data IN endpoint.
   * @return true 表示本次已经发送 ZLP，completion 处理应立即返回。
   * @return true when a ZLP has been sent and completion handling should return.
   */
  bool TrySendPendingZlp(Endpoint& ep)
  {
    if (!tx_state_.NeedZlp())
    {
      return false;
    }

    if (ep.GetActiveLength() == 0 && !tx_deq_.HasOp())
    {
      auto z = ep.TransferZLP();
      ASSERT(z == ErrorCode::OK);
      tx_state_.ClearZlp();
      return true;
    }

    tx_state_.ClearZlp();
    return false;
  }

  /**
   * @brief 若当前 head op 已完成，则弹出 info 并完成一次 WritePort 请求。
   * @brief Finish one WritePort request when the current head operation is complete.
   * @param in_isr 是否在 ISR 上下文。 Whether the call runs in ISR context.
   */
  void FinishCompletedTxHead(bool in_isr)
  {
    if (!tx_deq_.HeadCompleted())
    {
      return;
    }

    WriteInfoBlock completed{};
    auto pop_ok = tx_deq_.PopCompleted(&completed);
    ASSERT(pop_ok == ErrorCode::OK);
    write_port_cdc_.Finish(in_isr, ErrorCode::OK, completed);
  }

  /**
   * @brief 根据刚发送的数据段和后续预写状态决定是否需要 ZLP。
   * @brief Decide whether a ZLP is required after the just-started data segment.
   * @param ep Data IN endpoint。 Data IN endpoint.
   * @param sent_length 刚启动 transfer 的数据长度。 Length of the just-started transfer.
   * @param prefilled 是否已经预写下一段。 Whether the next segment was prefetched.
   */
  void RequestZlpIfNeeded(Endpoint& ep, std::size_t sent_length, bool prefilled)
  {
    const std::size_t MPS = ep.MaxPacketSize();
    if (!prefilled && sent_length > 0 && MPS > 0 && (sent_length % MPS) == 0 &&
        ep.GetActiveLength() == 0 && !tx_deq_.HasOp())
    {
      tx_state_.RequestZlp();
    }
  }

  /**
   * @brief 从 TX queue 预写一段数据到 endpoint active buffer。
   * @brief Prefill one TX segment into the endpoint active buffer.
   * @param ep Data IN endpoint。 Data IN endpoint.
   * @param prefilled 输出是否写入了 active length。 Outputs whether active length was
   * set.
   * @return `OK` 表示该 op 已全部预写；`PENDING` 表示该 op 仍有剩余。
   * @return `OK` when the operation is fully prefetched; `PENDING` when bytes remain.
   */
  ErrorCode PrefillTxEndpoint(Endpoint& ep, bool& prefilled)
  {
    prefilled = false;
    auto buffer = ep.GetBuffer();
    std::size_t len = 0;

    auto ec = tx_deq_.Take(reinterpret_cast<uint8_t*>(buffer.addr_), buffer.size_, len);
    if (ec == ErrorCode::EMPTY || len == 0)
    {
      return ErrorCode::PENDING;
    }
    if (ec != ErrorCode::OK && ec != ErrorCode::PENDING)
    {
      return ec;
    }

    ep.SetActiveLength(len);
    prefilled = true;
    return ec;
  }

  CDCUartReadPort read_port_cdc_;    ///< CDC RX 读端口 / CDC RX read port
  LibXR::WritePort write_port_cdc_;  ///< CDC TX 写端口 / CDC TX write port

  LibXR::CDCUartTxOpDequeueHelper tx_deq_;  ///< TX 出队辅助器 / TX dequeue helper

  CDCUartTxState tx_state_;  ///< CDC TX 控制状态 / CDC TX control state
};

inline void CDCUartReadPort::OnRxDequeue(bool in_isr)
{
  if (!rx_backpressure_.Paused())
  {
    return;
  }

  if (!rx_backpressure_.FlushPending(in_isr))
  {
    return;
  }

  // 尝试恢复 rearm / Try to rearm OUT
  (void)owner_.TryRearmOut(in_isr);
}

}  // namespace LibXR::USB
