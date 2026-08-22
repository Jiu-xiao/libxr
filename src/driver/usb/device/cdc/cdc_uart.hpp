#pragma once

#include <algorithm>

#include "cdc_base.hpp"
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
  CDCUart(
      Endpoint::EPNumber data_in_ep_num, Endpoint::EPNumber data_out_ep_num,
      Endpoint::EPNumber comm_ep_num, size_t rx_buffer_size = 128,
      size_t tx_buffer_size = 128, size_t tx_queue_size = 5,
      const char* control_interface_string = CDCBase::DEFAULT_CONTROL_INTERFACE_STRING,
      const char* data_interface_string = CDCBase::DEFAULT_DATA_INTERFACE_STRING)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num, control_interface_string,
                data_interface_string),
        LibXR::UART(&read_port_cdc_, &write_port_cdc_),
        read_port_cdc_(rx_buffer_size, *this),
        write_port_cdc_(tx_queue_size, tx_buffer_size)
  {
    write_port_cdc_ = WriteFun;  // NOLINT
  }

  /**
   * @brief 设置 UART 配置（CDC Line Coding）
   *        Set UART configuration (CDC Line Coding)
   *
   * @param cfg UART 配置 / UART configuration
   * @param in_isr 是否从 ISR 上下文调用；普通任务上下文可省略，默认值为 false / Whether
   * called from ISR context; ordinary task context may omit it and defaults to false
   * @return 错误码 / Error code
   */
  ErrorCode SetConfig(UART::Configuration cfg, bool in_isr = false) override
  {
    UNUSED(in_isr);
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
    (void)SendSerialState();
    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 尝试 rearm OUT（背压恢复/持续接收）
   *        Try to rearm OUT endpoint (backpressure recovery / continuous RX)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @return true 成功 rearm / Rearmed successfully
   * @return false 未 rearm（忙/空间不足/端点不可用）/ Not rearmed (busy, insufficient
   * space, or endpoint unavailable).
   */
  bool TryRearmOut(bool in_isr)
  {
    auto ep_data_out = GetDataOutEndpoint();
    if (ep_data_out == nullptr)
    {
      return false;
    }

    const std::size_t MPS = ep_data_out->MaxPacketSize();
    if (MPS == 0U || read_port_cdc_.Capacity() == 0U)
    {
      return false;
    }

    if (read_port_cdc_.recv_pause_ && read_port_cdc_.pending_data_.size_ > 0U)
    {
      auto queue = read_port_cdc_.GetReadQueue(in_isr);
      auto push_ans = queue.PushBatch(
          reinterpret_cast<const uint8_t*>(read_port_cdc_.pending_data_.addr_),
          read_port_cdc_.pending_data_.size_);
      if (push_ans == ErrorCode::OK)
      {
        read_port_cdc_.pending_data_ = {nullptr, 0};
        queue.Publish();
      }
      else
      {
        return false;
      }
    }

    const Endpoint::State state = ep_data_out->GetState();
    if (state == Endpoint::State::BUSY)
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
   * @brief 解绑端点并重置 endpoint-local TX/RX 状态
   *        Unbind endpoints and reset endpoint-local TX/RX state
   *
   * @param endpoint_pool 端点资源池 / Endpoint resource pool
   * @param in_isr 是否在 ISR 上下文 / Whether in ISR context
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr) override
  {
    CDCBase::UnbindEndpoints(endpoint_pool, in_isr);
    ResetTxState();
    read_port_cdc_.recv_pause_ = false;
    read_port_cdc_.pending_data_ = {nullptr, 0};
  }

  /**
   * @brief 写端口回调（TX）
   *        Write port callback (TX)
   *
   * 该入口同步推进有界 WriteQueue、填充 ACTIVE/READY、启动 endpoint 并处理 ZLP。/ This
   * entry synchronously advances the bounded WriteQueue, fills ACTIVE/READY, starts the
   * endpoint, and handles ZLP.
   *
   * @param port  写端口 / Write port
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   */
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);
    if (cdc->in_write_isr_.IsSet())
    {
      return;
    }
    cdc->ServiceTx(false, in_isr);
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
      auto queue = read_port_cdc_.GetReadQueue(in_isr);
      auto push_ans =
          queue.PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_);
      if (push_ans == ErrorCode::OK)
      {
        queue.Publish();
      }
      else if (push_ans == ErrorCode::FULL)
      {
        read_port_cdc_.recv_pause_ = true;
        read_port_cdc_.pending_data_ = data;
        return;
      }
      else
      {
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
    ServiceTx(true, in_isr);
  }

 private:
  friend class CDCUartReadPort;

  enum class TxPhase : uint8_t
  {
    IDLE,
    DATA,
    ZLP,
  };

  /** @brief 丢弃未完成的 endpoint-local TX 槽 / Discard endpoint-local TX slots. */
  void ResetTxState()
  {
    tx_phase_ = TxPhase::IDLE;
    tx_active_length_ = 0U;
    tx_ready_length_ = 0U;
    need_write_zlp_ = false;
  }

  /**
   * @brief 将一个请求片段复制到当前 endpoint buffer / Copy one request fragment into
   * the current endpoint buffer
   */
  std::size_t CopyFrontToEndpoint(Endpoint& ep, WritePort::WriteQueue& queue,
                                  std::size_t request_limit)
  {
    const RawData buffer = ep.GetBuffer();
    const std::size_t capacity = std::min(buffer.size_, ep.MaxTransferSize());
    const std::size_t limit = std::min(request_limit, capacity);
    if (limit == 0U || buffer.addr_ == nullptr)
    {
      return 0U;
    }

    return queue.PopWithWriter(
        limit,
        [buffer](const uint8_t* first, std::size_t first_size, const uint8_t* second,
                 std::size_t second_size) -> std::size_t
        {
          auto* destination = reinterpret_cast<uint8_t*>(buffer.addr_);
          if (first_size != 0U)
          {
            Memory::FastCopy(destination, first, first_size);
          }
          if (second_size != 0U)
          {
            Memory::FastCopy(destination + first_size, second, second_size);
          }
          return first_size + second_size;
        });
  }

  /** @brief 启动已接纳 DATA / Start accepted DATA. */
  bool StartAcceptedData(Endpoint& ep, std::size_t length)
  {
    ASSERT(length != 0U);
    tx_phase_ = TxPhase::DATA;
    tx_active_length_ = length;
    need_write_zlp_ = false;
    if (ep.Transfer(length) == ErrorCode::OK)
    {
      return true;
    }

    ResetTxState();
    return false;
  }

  /**
   * @brief 填充空闲 ACTIVE 和可选 READY / Fill an idle ACTIVE and optional READY
   */
  void FillDataSlots(Endpoint& ep, bool in_isr)
  {
    if (tx_phase_ == TxPhase::IDLE)
    {
      if (ep.GetState() != Endpoint::State::IDLE)
      {
        return;
      }

      auto queue = write_port_cdc_.GetWriteQueue(in_isr);
      if (queue.front_size == 0U)
      {
        return;
      }

      const std::size_t accepted = CopyFrontToEndpoint(ep, queue, queue.front_size);
      if (accepted == 0U || !StartAcceptedData(ep, accepted))
      {
        return;
      }

      if (!ep.UseDoubleBuffer())
      {
        return;
      }

      const std::size_t ready_limit =
          accepted < queue.front_size ? queue.front_size - accepted : queue.next_size;
      if (ready_limit != 0U)
      {
        tx_ready_length_ = CopyFrontToEndpoint(ep, queue, ready_limit);
      }
      return;
    }

    if (ep.UseDoubleBuffer() && tx_phase_ == TxPhase::DATA && tx_ready_length_ == 0U)
    {
      const Endpoint::State state = ep.GetState();
      if (state != Endpoint::State::BUSY && state != Endpoint::State::IDLE)
      {
        return;
      }

      auto queue = write_port_cdc_.GetWriteQueue(in_isr);
      if (queue.front_size != 0U)
      {
        tx_ready_length_ = CopyFrontToEndpoint(ep, queue, queue.front_size);
      }
    }
  }

  /** @brief 退休完成的 DATA/ZLP，并优先启动 READY / Retire completed DATA/ZLP and
   * start READY first. */
  void RetireActiveTx(Endpoint& ep)
  {
    if (tx_phase_ == TxPhase::ZLP)
    {
      tx_phase_ = TxPhase::IDLE;
      need_write_zlp_ = false;
      return;
    }
    if (tx_phase_ != TxPhase::DATA)
    {
      return;
    }

    const std::size_t max_packet_size = ep.MaxPacketSize();
    need_write_zlp_ = max_packet_size > 0U && tx_active_length_ > 0U &&
                      (tx_active_length_ % max_packet_size) == 0U;
    tx_active_length_ = 0U;
    tx_phase_ = TxPhase::IDLE;

    if (tx_ready_length_ == 0U)
    {
      return;
    }
    if (ep.GetState() != Endpoint::State::IDLE)
    {
      ResetTxState();
      return;
    }

    const std::size_t ready_length = tx_ready_length_;
    tx_ready_length_ = 0U;
    (void)StartAcceptedData(ep, ready_length);
  }

  /**
   * @brief 在 producer admission 下稳定确认空队列并启动一个 ZLP
   *        Start one ZLP after stable queue-idle admission
   */
  void TryStartZlp(Endpoint& ep, bool in_isr)
  {
    (void)write_port_cdc_.TryRunWhenWriteQueueIdle(
        [this, &ep]
        {
          tx_phase_ = TxPhase::ZLP;
          if (ep.TransferZLP() != ErrorCode::OK)
          {
            ResetTxState();
            return;
          }
          need_write_zlp_ = false;
        },
        in_isr);
  }

  /**
   * @brief 在当前同步入口推进 CDC TX 状态机
   *        Advance the CDC TX state machine from the current synchronous entry
   */
  void ServiceTx(bool in_complete, bool in_isr)
  {
    if (!Inited())
    {
      return;
    }

    Endpoint* ep = GetDataInEndpoint();
    if (ep == nullptr)
    {
      return;
    }

    if (in_complete)
    {
      RetireActiveTx(*ep);
    }

    if (tx_phase_ == TxPhase::DATA && ep->GetState() == Endpoint::State::ERROR)
    {
      ResetTxState();
      return;
    }

    FillDataSlots(*ep, in_isr);

    if (tx_phase_ == TxPhase::IDLE && need_write_zlp_ &&
        ep->GetState() == Endpoint::State::IDLE)
    {
      TryStartZlp(*ep, in_isr);
    }
  }

  CDCUartReadPort read_port_cdc_;  ///< CDC RX 读端口 / CDC RX read port
  WritePort write_port_cdc_;       ///< CDC TX 写端口 / CDC TX write port

  Flag::Plain in_write_isr_;  ///< 写 ISR 保护标志 / Write ISR guard flag
  TxPhase tx_phase_ = TxPhase::IDLE;
  std::size_t tx_active_length_ = 0U;
  std::size_t tx_ready_length_ = 0U;
  bool need_write_zlp_ = false;
};

inline void CDCUartReadPort::OnRxDequeue(bool in_isr)
{
  if (!recv_pause_)
  {
    return;
  }

  if (pending_data_.size_ > 0U)
  {
    auto queue = GetReadQueue(in_isr);
    if (queue.PushBatch(reinterpret_cast<const uint8_t*>(pending_data_.addr_),
                        pending_data_.size_) != ErrorCode::OK)
    {
      return;
    }
    pending_data_ = {nullptr, 0};
    queue.Publish();
  }

  (void)owner_.TryRearmOut(in_isr);
}

}  // namespace LibXR::USB
