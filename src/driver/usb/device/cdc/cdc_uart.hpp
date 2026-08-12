#pragma once

#include <atomic>

#include "cdc_base.hpp"
#include "driver/common/serialized_service.hpp"
#include "ep.hpp"
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
  explicit CDCUartTxOpDequeueHelper(WritePort& port) : port_(port) {}

  /** @return 是否存在尚未完全接受的写操作 / Whether an unaccepted write remains. */
  bool HasOp() { return head_valid_ || (port_.queue_info_->Size() > 0); }

  /** @return 是否有仅存在于当前 endpoint buffer 的待提交数据 / Whether a stage is
   * prepared. */
  bool HasPrepared() const { return prepared_len_ != 0U; }

  /**
   * @brief 将下一段复制到 endpoint buffer，但不消费 WritePort 数据
   *        Copy the next chunk into the endpoint buffer without consuming WritePort data
   * @param dst endpoint buffer / Endpoint buffer
   * @param cap endpoint buffer capacity / Endpoint buffer capacity
   * @param out_len prepared byte count / Prepared byte count
   * @return `OK` 表示该段可结束当前操作，`PENDING` 表示后续仍有数据 /
   *         `OK` when this chunk can finish the operation, otherwise `PENDING`
   */
  ErrorCode Prepare(uint8_t* dst, std::size_t cap, std::size_t& out_len)
  {
    if (prepared_len_ != 0U)
    {
      out_len = prepared_len_;
      return PreparedCompletesHead() ? ErrorCode::OK : ErrorCode::PENDING;
    }

    const ErrorCode head_result = EnsureHead();
    if (head_result != ErrorCode::OK)
    {
      out_len = 0U;
      return head_result;
    }

    const std::size_t remaining = Remaining();
    if (remaining == 0U || cap == 0U)
    {
      out_len = 0U;
      return ErrorCode::FAILED;
    }

    const std::size_t prepare_len = (remaining < cap) ? remaining : cap;
    if (port_.queue_data_->PeekBatch(dst, prepare_len) != ErrorCode::OK)
    {
      out_len = 0U;
      return ErrorCode::FAILED;
    }

    prepared_len_ = prepare_len;
    out_len = prepare_len;
    return PreparedCompletesHead() ? ErrorCode::OK : ErrorCode::PENDING;
  }

  /**
   * @brief 在 endpoint 接受 Transfer 后提交已准备的数据
   *        Commit prepared data after the endpoint accepts Transfer
   */
  ErrorCode CommitPrepared()
  {
    if (!head_valid_ || prepared_len_ == 0U)
    {
      return ErrorCode::FAILED;
    }

    const std::size_t committed_len = prepared_len_;
    if (port_.queue_data_->PopBatch(nullptr, committed_len) != ErrorCode::OK)
    {
      return ErrorCode::FAILED;
    }

    accepted_ += committed_len;
    prepared_len_ = 0U;
    return HeadCompleted() ? ErrorCode::OK : ErrorCode::PENDING;
  }

  /**
   * @brief 丢弃仅存在于旧 endpoint buffer 中的 staging 状态
   *        Discard staging state that exists only in the old endpoint buffer
   */
  void CancelPrepared() { prepared_len_ = 0U; }

  /** @return 当前操作是否已被 endpoint 全部接受 / Whether the head is fully accepted. */
  bool HeadCompleted() const
  {
    return head_valid_ && prepared_len_ == 0U && accepted_ == head_.data.size_;
  }

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
    if (ans != ErrorCode::OK)
    {
      return ans;
    }

    if (completed_info)
    {
      *completed_info = popped;
    }

    ResetHead();
    return ErrorCode::OK;
  }

 private:
  void ResetHead()
  {
    head_valid_ = false;
    accepted_ = 0U;
    prepared_len_ = 0U;
  }

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
    accepted_ = 0U;
    return ErrorCode::OK;
  }

  bool PreparedCompletesHead() const
  {
    ASSERT(head_valid_);
    ASSERT(head_.data.size_ >= accepted_);
    return prepared_len_ == (head_.data.size_ - accepted_);
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
    ASSERT(head_.data.size_ >= accepted_);
    return head_.data.size_ - accepted_;
  }

 private:
  WritePort& port_;                ///< 写端口引用 / Write port reference
  bool head_valid_ = false;        ///< head 缓存有效标志 / Cached head valid flag
  WriteInfoBlock head_{};          ///< 缓存的 head info / Cached head info
  std::size_t accepted_ = 0U;      ///< 已被 endpoint 接受的字节数 / Accepted bytes
  std::size_t prepared_len_ = 0U;  ///< 尚未提交的 staging 长度 / Uncommitted stage
};

}  // namespace LibXR

namespace LibXR::USB
{

class CDCUart;

/**
 * @brief CDC UART 读端口（完整 OUT packet 预留背压）
 *        CDC UART read port with whole-OUT-packet reservation backpressure
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

  CDCUart& owner_;  ///< 所属 CDCUart / Owning CDCUart

  std::atomic<bool> rx_space_waiting_{
      false};  ///< 是否等待软件 RX 队列释放一个完整 OUT packet 的空间 /
               ///< Whether OUT waits for one whole packet of software RX queue space
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
      Endpoint::EPNumber data_in_ep_num, Endpoint::EPNumber data_out_ep_num,
      Endpoint::EPNumber comm_ep_num, size_t rx_buffer_size = 128,
      size_t tx_buffer_size = 128, size_t tx_queue_size = 5,
      const char* control_interface_string = CDCBase::DEFAULT_CONTROL_INTERFACE_STRING,
      const char* data_interface_string = CDCBase::DEFAULT_DATA_INTERFACE_STRING)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num, control_interface_string,
                data_interface_string, false),
        LibXR::UART(&read_port_cdc_, &write_port_cdc_),
        read_port_cdc_(rx_buffer_size, *this),
        write_port_cdc_(tx_queue_size, tx_buffer_size),
        tx_deq_(write_port_cdc_)
  {
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
   * @return true OUT 已经 armed 或本次成功 rearm / OUT is already armed or was rearmed
   * @return false 未 rearm（空间不足或端点不可用）/ Not rearmed (insufficient space or
   * endpoint unavailable)
   */
  bool TryRearmOut(bool in_isr)
  {
    bool rearmed = false;
    (void)rx_service_.Invoke(RX_KICK,
                             [this, in_isr, &rearmed](uint32_t events) noexcept
                             {
                               UNUSED(events);
                               rearmed = ServiceRxRearm(in_isr);
                             });
    return rearmed;
  }

 protected:
  /**
   * @brief 绑定 endpoint 并恢复解绑期间保留的发送操作
   *        Bind endpoints and resume writes retained while unbound
   */
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                     bool in_isr) override
  {
    CDCBase::BindEndpoints(endpoint_pool, start_itf_num, in_isr);
    (void)TryRearmOut(in_isr);
    RunTxService(in_isr);
  }

  /**
   * @brief 解绑 endpoint，并保留尚未被 endpoint 接受的读写操作
   *        Unbind endpoints while retaining operations not yet accepted by endpoints
   *
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param in_isr        是否在 ISR 上下文 / In ISR context
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr) override
  {
    tx_deq_.CancelPrepared();
    need_write_zlp_ = false;
    CDCBase::UnbindEndpoints(endpoint_pool, in_isr);
    read_port_cdc_.rx_space_waiting_.store(false, std::memory_order_release);
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
   * - staging 只复制数据；Transfer 接受后才提交对应队列前缀
   *   Staging only copies data; the queue prefix is committed after Transfer accepts it
   *
   * @param port  写端口 / Write port
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @return 错误码 / Error code
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr)
  {
    auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);
    cdc->RunTxService(in_isr);
    return ErrorCode::PENDING;
  }

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
      const ErrorCode push_ans = read_port_cdc_.queue_data_->PushBatch(
          reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
      if (push_ans == ErrorCode::OK)
      {
        read_port_cdc_.ProcessPendingReads(in_isr);
      }
      else
      {
        REQUIRE_FROM_CALLBACK(push_ans == ErrorCode::OK, in_isr);
        read_port_cdc_.rx_space_waiting_.store(true, std::memory_order_release);
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
    RunTxService(in_isr);
  }

 private:
  static constexpr uint32_t TX_KICK = 1U;
  static constexpr uint32_t RX_KICK = 1U;

  bool ServiceRxRearm(bool in_isr)
  {
    auto* ep_data_out = GetDataOutEndpoint();
    if (ep_data_out == nullptr)
    {
      return false;
    }

    const std::size_t max_packet_size = ep_data_out->MaxPacketSize();
    const bool valid_storage = max_packet_size > 0U &&
                               read_port_cdc_.queue_data_ != nullptr &&
                               read_port_cdc_.queue_data_->MaxSize() >= max_packet_size;
    if (!valid_storage)
    {
      read_port_cdc_.rx_space_waiting_.store(true, std::memory_order_release);
      REQUIRE_FROM_CALLBACK(valid_storage, in_isr);
      return false;
    }

    // Publish the wait before inspecting space. A dequeue before publication is observed
    // by the following queue check; a dequeue after publication publishes RX_KICK.
    // 先发布 waiting 再检查空间：先发生的 dequeue 由下面的复查承接，
    // 后发生的 dequeue 则会发布 RX_KICK。
    read_port_cdc_.rx_space_waiting_.exchange(true, std::memory_order_acq_rel);
    if (read_port_cdc_.queue_data_->EmptySize() < max_packet_size)
    {
      return false;
    }

    if (ep_data_out->GetState() == Endpoint::State::BUSY)
    {
      read_port_cdc_.rx_space_waiting_.store(false, std::memory_order_release);
      return true;
    }

    const bool idle = ep_data_out->GetState() == Endpoint::State::IDLE;
    if (!idle)
    {
      REQUIRE_FROM_CALLBACK(idle, in_isr);
      return false;
    }

    // Publish the optimistic state before Transfer(). A synchronous OUT completion only
    // queues another RX_KICK; the serialized owner applies its final backpressure state
    // after Transfer() returns.
    // Transfer() 前先发布乐观状态。同步 OUT completion 只会追加 RX_KICK；Transfer()
    // 返回后 仍由同一个串行 owner 决定最终背压状态。
    read_port_cdc_.rx_space_waiting_.store(false, std::memory_order_release);
    const ErrorCode result = ep_data_out->Transfer(max_packet_size);
    if (result != ErrorCode::OK)
    {
      read_port_cdc_.rx_space_waiting_.store(true, std::memory_order_release);
      REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
      return false;
    }
    return true;
  }

  void RunTxService(bool in_isr)
  {
    (void)tx_service_.Invoke(TX_KICK,
                             [this, in_isr](uint32_t events) noexcept
                             {
                               UNUSED(events);
                               ServiceTx(in_isr);
                             });
  }

  void ServiceTx(bool in_isr)
  {
    WriteInfoBlock completed{};
    if (PumpTx(in_isr, completed))
    {
      write_port_cdc_.Finish(in_isr, ErrorCode::OK, completed);
    }
  }

  bool PrepareTxStage(Endpoint& ep, bool in_isr)
  {
    if (ep.GetActiveLength() != 0U)
    {
      REQUIRE_FROM_CALLBACK(tx_deq_.HasPrepared(), in_isr);
      return true;
    }
    REQUIRE_FROM_CALLBACK(!tx_deq_.HasPrepared(), in_isr);
    if (!tx_deq_.HasOp())
    {
      return false;
    }

    RawData buffer = ep.GetBuffer();
    const std::size_t capacity = (buffer.size_ < static_cast<std::size_t>(UINT16_MAX))
                                     ? buffer.size_
                                     : static_cast<std::size_t>(UINT16_MAX);
    std::size_t prepared_len = 0U;
    const ErrorCode prepare_result =
        tx_deq_.Prepare(reinterpret_cast<uint8_t*>(buffer.addr_), capacity, prepared_len);
    if (prepare_result == ErrorCode::EMPTY)
    {
      return false;
    }
    REQUIRE_FROM_CALLBACK(
        (prepare_result == ErrorCode::OK || prepare_result == ErrorCode::PENDING) &&
            prepared_len > 0U,
        in_isr);

    ep.SetActiveLength(static_cast<uint16_t>(prepared_len));
    return true;
  }

  /**
   * @brief 尝试启动一个 TX chunk，并在成功后提交对应队列前缀
   *        Try to start one TX chunk and commit its queue prefix only after success
   * @param in_isr 是否在 ISR 上下文 / Whether running in ISR context
   * @param completed final chunk 接受后返回的操作元数据 / Operation metadata returned
   *                  after a final chunk is accepted
   * @return 是否完成了一个写操作 / Whether one write operation completed
   */
  bool PumpTx(bool in_isr, WriteInfoBlock& completed)
  {
    auto* ep = GetDataInEndpoint();
    if (ep == nullptr || !Inited())
    {
      return false;
    }

    if (tx_deq_.HasOp())
    {
      need_write_zlp_ = false;
    }
    else if (need_write_zlp_)
    {
      if (ep->GetActiveLength() == 0U && ep->GetState() == Endpoint::State::IDLE &&
          ep->TransferZLP() == ErrorCode::OK)
      {
        need_write_zlp_ = false;
      }
      return false;
    }

    if (!PrepareTxStage(*ep, in_isr) || ep->GetState() != Endpoint::State::IDLE)
    {
      return false;
    }

    const std::size_t transfer_len = ep->GetActiveLength();
    if (transfer_len == 0U)
    {
      return false;
    }

    ep->SetActiveLength(0U);
    if (ep->Transfer(transfer_len) != ErrorCode::OK)
    {
      // Some endpoint implementations may rotate buffers before reporting failure.
      tx_deq_.CancelPrepared();
      return false;
    }

    const ErrorCode commit_result = tx_deq_.CommitPrepared();
    REQUIRE_FROM_CALLBACK(
        commit_result == ErrorCode::OK || commit_result == ErrorCode::PENDING, in_isr);

    const bool operation_completed = commit_result == ErrorCode::OK;
    if (operation_completed)
    {
      const ErrorCode pop_result = tx_deq_.PopCompleted(&completed);
      REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);
    }

    const bool next_stage_prepared = PrepareTxStage(*ep, in_isr);
    const std::size_t max_packet_size = ep->MaxPacketSize();
    if (operation_completed && !next_stage_prepared && max_packet_size > 0U &&
        (transfer_len % max_packet_size) == 0U)
    {
      need_write_zlp_ = true;
    }

    return operation_completed;
  }

  CDCUartReadPort read_port_cdc_;    ///< CDC RX 读端口 / CDC RX read port
  LibXR::WritePort write_port_cdc_;  ///< CDC TX 写端口 / CDC TX write port

  LibXR::CDCUartTxOpDequeueHelper tx_deq_;  ///< TX 出队辅助器 / TX dequeue helper

  SerializedService rx_service_{};  ///< RX rearm 的唯一 owner / RX rearm owner
  SerializedService tx_service_{};  ///< TX endpoint/helper 的唯一 owner / TX owner

  bool need_write_zlp_{false};  ///< ZLP 需求标志 / ZLP required flag
};

inline void CDCUartReadPort::OnRxDequeue(bool in_isr)
{
  if (!rx_space_waiting_.exchange(false, std::memory_order_acq_rel))
  {
    return;
  }

  (void)owner_.TryRearmOut(in_isr);
}

}  // namespace LibXR::USB
