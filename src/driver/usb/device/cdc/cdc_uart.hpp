#pragma once

#include <algorithm>
#include <atomic>

#include "cdc_base.hpp"
#include "ep.hpp"
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
 *
 * @note 所属 USB device 的唯一 execution policy 串行化运行期 raw IRQ、endpoint
 *       生命周期、Write doorbell、IN completion 与 OUT rearm。启动前或停止后的
 *       Init/Deinit 必须处于 IRQ 已禁用的 quiescent context。Endpoint::Close() 是旧完成
 *       源的 quiescence 点。 / The owning USB device's sole execution policy serializes
 *       runtime raw IRQs, endpoint lifecycle, Write doorbells, IN completions, and OUT
 *       rearm. Pre-start or post-stop Init/Deinit must run in a quiescent context with
 *       IRQs disabled. Endpoint::Close() is the quiescence point for old completion
 *       sources.
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
   * @return 错误码 / Error code
   */
  ErrorCode SetConfig(UART::Configuration cfg) override
  {
    switch (cfg.stop_bits)
    {
      case 1:
      case 2:
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    switch (cfg.parity)
    {
      case UART::Parity::NO_PARITY:
      case UART::Parity::ODD:
      case UART::Parity::EVEN:
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
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    uint32_t expected = 0U;
    if (!config_pending_.compare_exchange_strong(expected, 1U, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed))
    {
      return ErrorCode::BUSY;
    }

    pending_config_ = cfg;
    PublishWork(CDC_EVENT_CONFIG, false);
    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 尝试 rearm OUT（背压恢复/持续接收）
   *        Try to rearm OUT endpoint (backpressure recovery / continuous RX)
   *
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   * @return true 成功 rearm / Rearmed successfully
   * @return false 未 rearm，必要时已封死当前 generation / Not rearmed; the current
   * generation is fail-stopped when the start path itself is invalid or fails.
   */
  bool TryRearmOut(bool in_isr)
  {
    if (!CanUseCdcEndpoints())
    {
      return false;
    }

    auto ep_data_out = GetDataOutEndpoint();
    if (ep_data_out == nullptr)
    {
      FailStopRxGeneration();
      return false;
    }

    const std::size_t MPS = ep_data_out->MaxPacketSize();
    if (MPS == 0U || read_port_cdc_.Capacity() == 0U)
    {
      FailStopRxGeneration();
      return false;
    }

    if (read_port_cdc_.recv_pause_)
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
        if (push_ans != ErrorCode::FULL)
        {
          FailStopRxGeneration();
        }
        return false;
      }
    }

    const Endpoint::State state = ep_data_out->GetState();
    if (state == Endpoint::State::BUSY || state == Endpoint::State::STALLED)
    {
      return false;
    }
    if (state != Endpoint::State::IDLE)
    {
      FailStopRxGeneration();
      return false;
    }

    auto ans = ep_data_out->Transfer(MPS);
    if (ans == ErrorCode::OK)
    {
      read_port_cdc_.recv_pause_ = false;
      return true;
    }

    // No OUT completion exists after an unsuccessful start, so there is no carrier that
    // could safely retry this arm. A configuration transition establishes the next owner.
    FailStopRxGeneration();
    return false;
  }

 protected:
  /** @brief 在 deferred OUT arm 前验证一个完整 endpoint packet 可入队 / Validate
   * one complete endpoint packet before the deferred OUT arm. */
  void OnEndpointsBound(bool in_isr) override
  {
    const Endpoint* ep_data_out = GetDataOutEndpoint();
    REQUIRE_FROM_CALLBACK(ep_data_out != nullptr, in_isr);
    REQUIRE_FROM_CALLBACK(ep_data_out->MaxPacketSize() > 0U, in_isr);
    REQUIRE_FROM_CALLBACK(read_port_cdc_.Capacity() >= ep_data_out->MaxPacketSize(),
                          in_isr);
    (void)TryRearmOut(in_isr);
  }

  /**
   * @brief 写端口回调（TX）
   *        Write port callback (TX)
   *
   * 这里只发布 level event。唯一 TX owner 构造有界 WriteQueue、填充 ACTIVE/READY、
   * 启动 endpoint 并处理 ZLP；递归 completion/write doorbell 只会合并事件。 / This
   * entry only publishes a level event. The sole TX owner constructs a bounded
   * WriteQueue, fills ACTIVE/READY, starts the endpoint, and handles ZLP. Recursive
   * completion/write doorbells only coalesce events.
   *
   * @param port  写端口 / Write port
   * @param in_isr 是否在 ISR 上下文 / In ISR context
   */
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);
    cdc->PublishWork(CDC_EVENT_WRITE, in_isr);
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
    if (!CanUseCdcEndpoints())
    {
      return;
    }

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
        FailStopRxGeneration();
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
    PublishWork(CDC_EVENT_DATA_IN_COMPLETE, in_isr);
  }

  /**
   * @brief 在所属 USB device owner 下推进 CDC 本地事件 / Advance local CDC events
   * under the owning USB device owner
   */
  void ProcessPendingWork(bool in_isr) noexcept override
  {
    const uint32_t events = pending_events_.exchange(0U, std::memory_order_acquire);
    const uint32_t base_events = TakeBaseWorkSnapshot();
    const bool lifecycle_changed = (base_events & BASE_EVENT_LIFECYCLE) != 0U;
    if (lifecycle_changed)
    {
      read_port_cdc_.recv_pause_ = false;
      read_port_cdc_.pending_data_ = {nullptr, 0};
    }
    if ((events & CDC_EVENT_CONFIG) != 0U)
    {
      ApplyPendingConfig(in_isr);
    }

    const bool tx_carrier = events != 0U || base_events != 0U;
    ServiceTx(events, lifecycle_changed, tx_carrier, in_isr);
    ProcessBaseWork(base_events, in_isr);

    if (!lifecycle_changed && (events & CDC_EVENT_RX_REARM) != 0U)
    {
      (void)TryRearmOut(in_isr);
    }
  }

 private:
  friend class CDCUartReadPort;

  enum class TxPhase : uint8_t
  {
    IDLE,
    DATA,
    ZLP,
  };

  static constexpr uint32_t CDC_EVENT_WRITE = 1U << 0U;
  static constexpr uint32_t CDC_EVENT_DATA_IN_COMPLETE = 1U << 1U;
  static constexpr uint32_t CDC_EVENT_RX_REARM = 1U << 2U;
  static constexpr uint32_t CDC_EVENT_CONFIG = 1U << 3U;

  /**
   * @brief 合并本地事件并唤醒所属 device owner / Coalesce a local event and wake the
   * owning device owner
   */
  void PublishWork(uint32_t events, bool in_isr) noexcept
  {
    pending_events_.fetch_or(events, std::memory_order_release);
    RequestPendingWork(in_isr);
  }

  /** @brief 在 device owner 下应用一笔已接纳的 CDC 配置 / Apply one admitted CDC
   * configuration under the device owner. */
  void ApplyPendingConfig(bool in_isr)
  {
    ASSERT(config_pending_.load(std::memory_order_acquire) != 0U);
    const UART::Configuration cfg = pending_config_;
    auto& line_coding = GetLineCoding();
    line_coding.dwDTERate = cfg.baudrate;
    line_coding.bCharFormat = cfg.stop_bits == 2U ? 2U : 0U;
    switch (cfg.parity)
    {
      case UART::Parity::ODD:
        line_coding.bParityType = 1U;
        break;
      case UART::Parity::EVEN:
        line_coding.bParityType = 2U;
        break;
      default:
        line_coding.bParityType = 0U;
        break;
    }
    line_coding.bDataBits = static_cast<uint8_t>(cfg.data_bits);
    config_pending_.store(0U, std::memory_order_release);
    PublishBaseWork(BASE_EVENT_SERIAL_STATE, in_isr);
  }

  /** @brief 丢弃 endpoint 已接纳槽并封死当前 generation / Discard accepted endpoint
   * slots and fail-stop the current generation. */
  void FailStopTxGeneration()
  {
    tx_phase_ = TxPhase::IDLE;
    tx_active_length_ = 0U;
    tx_ready_length_ = 0U;
    need_write_zlp_ = false;
    FailStopCurrentGeneration();
  }

  /** @brief 封死无法重新 arm OUT 的 generation / Fail-stop a generation whose OUT
   * start path cannot make progress. */
  void FailStopRxGeneration()
  {
    read_port_cdc_.recv_pause_ = false;
    read_port_cdc_.pending_data_ = {nullptr, 0};
    FailStopCurrentGeneration();
  }

  /** @brief 在 lifecycle 边界丢弃 ACTIVE/READY / Discard ACTIVE/READY at a lifecycle
   * boundary. */
  void ResetTxForLifecycle()
  {
    tx_generation_ = DeviceGeneration();
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

  /** @brief 启动已接纳 DATA；失败时 fail-stop / Start accepted DATA, fail-stopping on
   * failure. */
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

    FailStopTxGeneration();
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
      FailStopTxGeneration();
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
            FailStopTxGeneration();
            return;
          }
          need_write_zlp_ = false;
        },
        in_isr);
  }

  /**
   * @brief 消费一个合并事件快照并推进 CDC TX 状态机
   *        Consume one coalesced event snapshot and advance the CDC TX state machine
   */
  void ServiceTx(uint32_t events, bool lifecycle_changed, bool tx_carrier, bool in_isr)
  {
    if (!tx_carrier)
    {
      return;
    }

    const bool generation_changed = tx_generation_ != DeviceGeneration();
    if (lifecycle_changed || generation_changed)
    {
      ResetTxForLifecycle();
    }

    if (!Inited() || !DeviceConfigured() || DeviceGenerationFatal())
    {
      return;
    }

    Endpoint* ep = GetDataInEndpoint();
    if (ep == nullptr)
    {
      return;
    }

    if (!lifecycle_changed && !generation_changed &&
        (events & CDC_EVENT_DATA_IN_COMPLETE) != 0U)
    {
      RetireActiveTx(*ep);
    }
    if (DeviceGenerationFatal())
    {
      return;
    }

    if (tx_phase_ == TxPhase::DATA && ep->GetState() == Endpoint::State::ERROR)
    {
      FailStopTxGeneration();
      return;
    }

    FillDataSlots(*ep, in_isr);
    if (DeviceGenerationFatal())
    {
      return;
    }

    if (tx_phase_ == TxPhase::IDLE && need_write_zlp_ &&
        ep->GetState() == Endpoint::State::IDLE)
    {
      TryStartZlp(*ep, in_isr);
    }
  }

  CDCUartReadPort read_port_cdc_;  ///< CDC RX 读端口 / CDC RX read port
  WritePort write_port_cdc_;       ///< CDC TX 写端口 / CDC TX write port

  std::atomic<uint32_t> pending_events_{0U};  ///< 本地 level events / Local level events
  std::atomic<uint32_t> config_pending_{
      0U};                                ///< 配置 mailbox admission / Config admission
  UART::Configuration pending_config_{};  ///< owner 消费的配置快照 / Owner snapshot
  TxPhase tx_phase_ = TxPhase::IDLE;
  uint32_t tx_generation_ = 0U;
  std::size_t tx_active_length_ = 0U;
  std::size_t tx_ready_length_ = 0U;
  bool need_write_zlp_ = false;
};

inline void CDCUartReadPort::OnRxDequeue(bool in_isr)
{
  owner_.PublishWork(CDCUart::CDC_EVENT_RX_REARM, in_isr);
}

}  // namespace LibXR::USB
