#pragma once

#include <atomic>

#include "cdc_base.hpp"
#include "ep.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR::USB
{

class CDCUart;

/**
 * @brief USB CDC 接收端口 / USB CDC receive port
 */
class CDCUartReadPort : public ReadPort
{
 public:
  /**
   * @brief 构造接收端口 / Construct the receive port
   * @param size 接收缓冲区大小 / Receive buffer size
   * @param owner 所属 CDC UART / Owning CDC UART
   */
  explicit CDCUartReadPort(uint32_t size, CDCUart& owner) : ReadPort(size), owner_(owner)
  {
  }

 protected:
  /**
   * @brief 在接收队列腾出空间后恢复接收 / Resume reception when queue space is available
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   */
  void OnReadQueueSpaceAvailable(bool in_isr) override;

 public:
  CDCUart& owner_;  ///< 所属 CDC UART / Owning CDC UART
};

/**
 * @brief USB CDC-ACM UART 适配器 / USB CDC-ACM UART adapter
 */
class CDCUart : public CDCBase, public LibXR::UART
{
 public:
  using LibXR::UART::Read;
  using LibXR::UART::read_port_;
  using LibXR::UART::Write;
  using LibXR::UART::write_port_;

  /**
   * @brief 构造 CDC UART / Construct the CDC UART
   * @param data_in_ep_num 数据 IN 端点号 / Data IN endpoint number
   * @param data_out_ep_num 数据 OUT 端点号 / Data OUT endpoint number
   * @param comm_ep_num 通信端点号 / Communication endpoint number
   * @param rx_buffer_size 接收队列容量 / Receive queue capacity
   * @param tx_buffer_size 发送数据队列容量 / Transmit data queue capacity
   * @param tx_queue_size 发送请求队列容量 / Transmit request queue capacity
   * @param control_interface_string 控制接口名称 / Control interface name
   * @param data_interface_string 数据接口名称 / Data interface name
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
   * @brief 设置 CDC 线路编码 / Set CDC line coding
   * @param cfg UART 配置 / UART configuration
   * @param in_isr 是否在中断中调用，本实现不使用 / ISR context, unused by this
   * implementation
   * @return 配置结果 / Configuration result
   */
  ErrorCode SetConfig(UART::Configuration cfg, bool in_isr = false) override
  {
    if (cfg.baudrate == 0U || (cfg.stop_bits != 1U && cfg.stop_bits != 2U) ||
        (cfg.parity != UART::Parity::NO_PARITY && cfg.parity != UART::Parity::ODD &&
         cfg.parity != UART::Parity::EVEN) ||
        (cfg.data_bits != 5U && cfg.data_bits != 6U && cfg.data_bits != 7U &&
         cfg.data_bits != 8U && cfg.data_bits != 16U))
      return ErrorCode::ARG_ERR;
    config_sequence_.fetch_add(1U, std::memory_order_acq_rel);
    config_baud_.store(cfg.baudrate, std::memory_order_relaxed);
    const uint32_t parity = cfg.parity == UART::Parity::ODD    ? 1U
                            : cfg.parity == UART::Parity::EVEN ? 2U
                                                               : 0U;
    config_format_.store((cfg.stop_bits == 2U ? 2U : 0U) | (parity << 8U) |
                             (static_cast<uint32_t>(cfg.data_bits) << 16U),
                         std::memory_order_relaxed);
    config_sequence_.fetch_add(1U, std::memory_order_release);
    RequestClassService(in_isr);
    return ErrorCode::OK;
  }

  void OnService(bool in_isr) override
  {
    const uint32_t before = config_sequence_.load(std::memory_order_acquire);
    if (before != applied_config_sequence_ && (before & 1U) == 0U)
    {
      const uint32_t baud = config_baud_.load(std::memory_order_relaxed);
      const uint32_t format = config_format_.load(std::memory_order_relaxed);
      if (config_sequence_.load(std::memory_order_acquire) == before)
      {
        auto& coding = GetLineCoding();
        coding.dwDTERate = baud;
        coding.bCharFormat = static_cast<uint8_t>(format);
        coding.bParityType = static_cast<uint8_t>(format >> 8U);
        coding.bDataBits = static_cast<uint8_t>(format >> 16U);
        applied_config_sequence_ = before;
        SendSerialState(in_isr);
      }
    }
    CDCBase::OnService(in_isr);
  }

  void OnDeviceEvent(bool in_isr, DeviceEvent event, uint8_t endpoint) override
  {
    if (event == DeviceEvent::ENDPOINT_HALTED)
    {
      if (auto* in = GetDataInEndpoint(); in && endpoint == in->GetAddress())
        need_write_zlp_ = false;
      if (auto* out = GetDataOutEndpoint(); out && endpoint == out->GetAddress())
        rx_offset_ = 0;
    }
    else if (event == DeviceEvent::ENDPOINT_RESUMED)
    {
      if (auto* out = GetDataOutEndpoint(); out && endpoint == out->GetAddress())
        TryRearmOut(in_isr);
      if (auto* in = GetDataInEndpoint(); in && endpoint == in->GetAddress())
        in->RequestTx(in_isr);
    }
  }

  /**
   * @brief 回填暂存数据并重新启动 OUT 接收 / Enqueue saved data and restart OUT reception
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   * @return 是否成功启动接收 / Whether reception was started
   */
  bool TryRearmOut(bool in_isr)
  {
    auto* ep = GetDataOutEndpoint();
    if (ep == nullptr || !read_port_cdc_.Readable()) return false;
    if (ep->HasReceiveResult())
    {
      const ConstRawData data = ep->ReceiveResult();
      while (rx_offset_ < data.size_)
      {
        auto queue = read_port_cdc_.GetReadQueue(in_isr);
        const size_t amount = LibXR::min(queue.EmptySize(), data.size_ - rx_offset_);
        if (amount == 0U)
        {
          queue.Publish();
          return false;
        }
        const auto result =
            queue.PushBatch(static_cast<const uint8_t*>(data.addr_) + rx_offset_, amount);
        DEV_ASSERT_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
        UNUSED(result);
        rx_offset_ += amount;  // Publish can callback; never expose the old offset.
        queue.Publish();
      }
    }
    if (ep->GetState() != Endpoint::State::IDLE && !ep->HasReceiveResult()) return false;
    rx_offset_ = 0;
    const auto result = ep->ArmReceive(ep->MaxPacketSize());
    DEV_ASSERT_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
    return result == ErrorCode::OK;
  }

 protected:
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t interface_number,
                     bool in_isr) override
  {
    CDCBase::BindEndpoints(endpoint_pool, interface_number, in_isr);
    auto* in = GetDataInEndpoint();
    auto* out = GetDataOutEndpoint();
    in->SetOnTxFill(tx_fill_cb_);
    in->SetOnTransferStarted(tx_started_cb_);
    out->SetOnWork(rx_work_cb_);
    tx_signal_.store(in, std::memory_order_release);
    rx_signal_.store(out, std::memory_order_release);
    in->RequestTx(in_isr);
  }

  /**
   * @brief 解绑端点并清理接收暂存状态 / Unbind endpoints and clear saved receive state
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool, bool in_isr) override
  {
    tx_signal_.store(nullptr, std::memory_order_release);
    rx_signal_.store(nullptr, std::memory_order_release);
    CDCBase::UnbindEndpoints(endpoint_pool, in_isr);
    need_write_zlp_ = false;
    rx_offset_ = 0;
  }

  /**
   * @brief 推进发送队列 / Advance the transmit queue
   * @param port 写端口 / Write port
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   */
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);
    if (auto* ep = cdc->tx_signal_.load(std::memory_order_acquire)) ep->RequestTx(in_isr);
  }

  /**
   * @brief 将 OUT 数据放入接收队列 / Enqueue completed OUT data
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   * @param data 收到的端点数据 / Received endpoint data
   */
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(data);
    rx_offset_ = 0;
    (void)TryRearmOut(in_isr);
  }

  /**
   * @brief 在 IN 完成后续发数据或零长度包 / Send more data or a ZLP after IN completion
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   * @param data 已发送的端点数据，本实现不使用 / Sent endpoint data, unused here
   */
  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    // WriteQueue settlement already completes admitted bytes in stable storage.
    // A zero-length transfer is retired by Endpoint exactly like ordinary DATA.
    UNUSED(in_isr);
    UNUSED(data);
  }

 private:
  /**
   * @brief 启动已填充的 IN 缓冲区 / Start the filled IN buffer
   * @param ep 数据 IN 端点 / Data IN endpoint
   * @param in_isr 是否在中断中调用 / Whether called in an ISR
   */
  void FillTx(bool in_isr, Endpoint::TxFill& fill)
  {
    {
      auto queue = write_port_cdc_.GetWriteQueue(in_isr);
      if (!queue.Empty())
      {
        const auto destination = fill.Buffer();
        const size_t size = queue.PopWithWriter(
            destination.size_,
            [destination](const uint8_t* first, size_t first_size, const uint8_t* second,
                          size_t second_size) -> size_t
            {
              auto* output = static_cast<uint8_t*>(destination.addr_);
              if (first_size) std::memcpy(output, first, first_size);
              if (second_size) std::memcpy(output + first_size, second, second_size);
              return first_size + second_size;
            });
        DEV_ASSERT_FROM_CALLBACK(size != 0U, in_isr);
        fill.SetSize(size);  // Required BEFORE queue destruction and user callbacks.
        need_write_zlp_ = (size % GetDataInEndpoint()->MaxPacketSize()) == 0U;
        return;
      }
    }
    if (fill.CanStart() && need_write_zlp_)
    {
      fill.SetSize(0);  // The tail is consumed only by a real hardware start.
    }
  }

  friend class CDCUartReadPort;
  void OnReceiveSpace(bool in_isr)
  {
    if (auto* ep = rx_signal_.load(std::memory_order_acquire)) ep->RequestService(in_isr);
  }

  CDCUartReadPort read_port_cdc_;    ///< CDC 接收端口 / CDC receive port
  LibXR::WritePort write_port_cdc_;  ///< CDC 发送端口 / CDC transmit port
  Callback<size_t> tx_started_cb_ = Callback<size_t>::Create(
      [](bool, CDCUart* self, size_t size)
      {
        self->need_write_zlp_ =
            size != 0U && size % self->GetDataInEndpoint()->MaxPacketSize() == 0U;
      },
      this);
  Callback<Endpoint::TxFill&> tx_fill_cb_ = Callback<Endpoint::TxFill&>::Create(
      [](bool context, CDCUart* self, Endpoint::TxFill& fill)
      { self->FillTx(context, fill); }, this);
  Callback<> rx_work_cb_ = Callback<>::Create(
      [](bool context, CDCUart* self) { (void)self->TryRearmOut(context); }, this);
  std::atomic<Endpoint*> tx_signal_{nullptr};
  std::atomic<Endpoint*> rx_signal_{nullptr};
  std::atomic<uint32_t> config_sequence_{0}, config_baud_{115200},
      config_format_{8U << 16U};
  uint32_t applied_config_sequence_ = 0;
  size_t rx_offset_ = 0;
  bool need_write_zlp_ = false;  ///< 待发送零长度包 / Pending zero-length packet
};

inline void CDCUartReadPort::OnReadQueueSpaceAvailable(bool in_isr)
{
  owner_.OnReceiveSpace(in_isr);
}

}  // namespace LibXR::USB
