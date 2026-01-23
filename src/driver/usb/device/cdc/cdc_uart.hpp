#pragma once
#include <cstring>

#include "cdc_base.hpp"

namespace LibXR::USB
{

/**
 * @brief USB CDC ACM UART 适配类
 *        USB CDC ACM UART adaptation class
 *
 * 作为 USB CDC 与 UART 之间的适配层：复用 CDCBase 完成 CDC 描述符与类请求处理，
 * 并通过 LibXR::UART 提供 Read/Write 等串口语义。
 *
 * Acts as an adaptation layer between USB CDC and UART:
 * reuses CDCBase for CDC descriptor setup and class request handling,
 * and exposes serial semantics via LibXR::UART (Read/Write).
 *
 * 继承自 CDCBase（间接继承 DeviceClass）与 LibXR::UART。
 * Inherits from CDCBase (thus DeviceClass indirectly) and LibXR::UART.
 */
class CDCUart : public CDCBase, public LibXR::UART
{
 public:
  // 公开 UART API 及底层端口对象（如需隐藏端口对象，可移除 read_port_/write_port_ 的
  // using）
  using LibXR::UART::Read;
  using LibXR::UART::read_port_;
  using LibXR::UART::Write;
  using LibXR::UART::write_port_;

  /**
   * @brief CDCUart 构造函数 / CDCUart constructor
   *
   * @param rx_buffer_size 接收缓冲区大小 / Receive buffer size
   * @param tx_buffer_size 发送缓冲区大小 / Transmit buffer size
   * @param tx_queue_size  发送队列容量 / Transmit queue capacity
   * @param data_in_ep_num  CDC 数据 IN 端点号 / CDC Data IN endpoint number
   * @param data_out_ep_num CDC 数据 OUT 端点号 / CDC Data OUT endpoint number
   * @param comm_ep_num     CDC 通知 IN 端点号（中断）/ CDC Notification IN endpoint
   * number (INT)
   */
  CDCUart(size_t rx_buffer_size = 128, size_t tx_buffer_size = 128,
          size_t tx_queue_size = 5,
          Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
          Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
          Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num),
        LibXR::UART(&read_port_cdc_, &write_port_cdc_),
        read_port_cdc_(rx_buffer_size),
        write_port_cdc_(tx_queue_size, tx_buffer_size)
  {
    // 初始化端口读写函数
    read_port_cdc_ = ReadFun;    // NOLINT
    write_port_cdc_ = WriteFun;  // NOLINT
  }

  /**
   * @brief 设置 UART 配置并同步到 CDC line coding
   *        Set UART configuration and sync to CDC line coding
   *
   * 将 UART 配置（波特率/数据位/停止位/校验）映射到 CDC line coding，并发送一次
   * Serial_State 通知以提示主机状态变化。注意：主机端最终生效的参数仍以其驱动为准，
   * 主机可能随后发出 GET/SET_LINE_CODING 进行协商。
   *
   * Maps UART configuration to CDC line coding and sends a Serial_State
   * notification. Note: the host ultimately decides the active settings and
   * may issue GET/SET_LINE_CODING afterwards.
   */
  ErrorCode SetConfig(UART::Configuration cfg) override
  {
    auto& line_coding = GetLineCoding();

    // 设置停止位
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

    // 设置校验位
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

    // 设置数据位
    switch (cfg.data_bits)
    {
      case 5:
        line_coding.bDataBits = 5;
        break;
      case 6:
        line_coding.bDataBits = 6;
        break;
      case 7:
        line_coding.bDataBits = 7;
        break;
      case 8:
        line_coding.bDataBits = 8;
        break;
      case 16:
        line_coding.bDataBits = 16;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    // 设置波特率
    line_coding.dwDTERate = cfg.baudrate;

    SendSerialState();

    return ErrorCode::OK;
  }

 protected:
  /**
   * @brief 反初始化 CDC 设备并清理写队列
   *        Deinitialize CDC device and drain TX queue
   *
   * 先调用 CDCBase::Deinit() 释放 USB 资源；随后将写队列中未完成的写请求
   * 以 INIT_ERR 完成并丢弃其数据，最后重置写端口内部状态。
   *
   * Calls CDCBase::Deinit(), then completes any pending TX requests with
   * INIT_ERR and drops their payloads, finally resets the write port.
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool) override
  {
    CDCBase::UnbindEndpoints(endpoint_pool);
    LibXR::WriteInfoBlock info;
    while (write_port_cdc_.queue_info_->Pop(info) == ErrorCode::OK)
    {
      write_port_cdc_.queue_data_->PopBatch(nullptr, info.data.size_);
      write_port_cdc_.Finish(true, ErrorCode::INIT_ERR, info, 0);
    }
    write_port_cdc_.Reset();
  }

  /**
   * @brief 写端口回调（从软件队列取数据并通过 CDC Data IN 端点发送）
   *        Write port callback (dequeue and transmit via CDC Data IN endpoint)
   *
   * - 若设备未初始化或 DTR 未置位：丢弃并以 INIT_ERR 完成当前写请求；
   * - 若端点忙或需要补发 ZLP：返回 FAILED 等待下次调度；
   * - 否则将分片拷贝到端点缓冲并启动传输。
   *
   * If device is not initialized or DTR is not asserted: drop and complete the
   * request with INIT_ERR. If the endpoint is busy or a ZLP is pending, return
   * FAILED to retry later. Otherwise copy a chunk to the endpoint buffer and start
   * the transfer.
   */
  static ErrorCode WriteFun(WritePort& port)
  {
    CDCUart* cdc = CONTAINER_OF(&port, CDCUart, write_port_cdc_);

    auto ep_data_in = cdc->GetDataInEndpoint();

    // 检查是否已初始化且DTR已设置
    if (!cdc->Inited() || !cdc->IsDtrSet())
    {
      if (ep_data_in->GetActiveLength() == 0)
      {
        WriteInfoBlock info;
        auto ans = port.queue_info_->Pop(info);
        if (ans != ErrorCode::OK)
        {
          return ErrorCode::EMPTY;
        }
        port.queue_data_->PopBatch(nullptr, info.data.size_);
        port.Finish(true, ErrorCode::INIT_ERR, info, 0);
      }
      return ErrorCode::FAILED;
    }

    size_t dequeued = 0;

    while (port.queue_info_->Size() > 0)
    {
      // 检查当前是否有传输正在进行
      if (ep_data_in->GetActiveLength() > 0 || cdc->need_write_zlp_)
      {
        return ErrorCode::FAILED;
      }

      auto buffer = ep_data_in->GetBuffer();

      WriteInfoBlock info;

      // 获取队列中的写操作信息
      if (port.queue_info_->Peek(info) != ErrorCode::OK)
      {
        return ErrorCode::EMPTY;
      }

      size_t need_write = info.data.size_ - dequeued;

      if (need_write == 0)
      {
        return ErrorCode::OK;
      }

      // 检查数据大小是否超出缓冲区
      if (need_write > buffer.size_)
      {
        need_write = buffer.size_;
      }

      // 从队列获取数据
      if (port.queue_data_->PopBatch(reinterpret_cast<uint8_t*>(buffer.addr_),
                                     need_write) != ErrorCode::OK)
      {
        ASSERT(false);
        return ErrorCode::EMPTY;
      }

      dequeued += need_write;

      cdc->written_ += need_write;

      if (cdc->written_ >= info.data.size_)
      {
        port.queue_info_->Pop();
        port.Finish(false, ErrorCode::OK, info, info.data.size_);
        cdc->written_ -= info.data.size_;
      }

      ep_data_in->SetActiveLength(need_write);

      // 保证对 ActiveLength/缓冲区的可见性顺序（与 ISR/USB 控制器交互）
      // Ensure visibility/order for ActiveLength/buffer updates across ISR/USB controller
      std::atomic_signal_fence(std::memory_order_seq_cst);

      // 如果端点空闲且有数据待发送
      if (ep_data_in->GetState() == Endpoint::State::IDLE &&
          ep_data_in->GetActiveLength() != 0)
      {
        /* 可以立即发送 */
      }
      else
      {
        return ErrorCode::FAILED;
      }

      // 启动传输
      auto ans = ErrorCode::OK;

      ep_data_in->SetActiveLength(0);

      ans = ep_data_in->Transfer(need_write);

      ASSERT(ans == ErrorCode::OK);
    }

    return ErrorCode::OK;
  }

  /**
   * @brief 读端口回调（占位）
   *        Read port callback (placeholder)
   *
   * 实际数据入队在 OUT 端点传输完成回调 OnDataOutComplete() 中完成，
   * 这里仅作为占位以满足接口。
   *
   * Actual enqueuing happens in OnDataOutComplete(); this is a placeholder.
   */
  static ErrorCode ReadFun(ReadPort& port)
  {
    UNUSED(port);
    return ErrorCode::EMPTY;
  }

  /**
   * @brief OUT 端点完成：预装下一次接收，并将本次数据推入软件缓冲
   *        Data OUT complete: arm next receive and push to software buffer
   *
   * 顺序：先重新启动 OUT 端点以持续接收；若本次 data.size_ > 0，
   * 则将其写入 read_port 的队列，并触发挂起的读取请求。
   *
   * Order: re-arm OUT endpoint first, then enqueue payload (if any) and
   * process pending reads.
   */
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    // 重启OUT端点传输
    auto ep_data_out = GetDataOutEndpoint();
    ep_data_out->Transfer(ep_data_out->MaxTransferSize());

    if (data.size_ > 0)
    {
      // 将接收到的数据推入接收缓冲区
      read_port_cdc_.queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                            data.size_);
      // 处理待处理的读取操作
      read_port_cdc_.ProcessPendingReads(in_isr);
    }
  }

  /**
   * @brief 数据IN端点传输完成处理
   *        Handle data IN endpoint transfer completion
   *
   * 完成发送操作并处理发送队列中的下一个数据包
   * Completes transmission and processes next packet in send queue
   */
  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);

    auto ep_data_in = GetDataInEndpoint();

    size_t pending_len = ep_data_in->GetActiveLength();

    if (pending_len == 0)
    {
      // TODO: zlp check
      return;
    }

    ep_data_in->SetActiveLength(0);
    auto ans = ep_data_in->Transfer(pending_len);
    ASSERT(ans == ErrorCode::OK);

    LibXR::WriteInfoBlock info;

    if (write_port_cdc_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return;
    }

    if (!Inited() || !IsDtrSet())
    {
      write_port_cdc_.queue_data_->PopBatch(nullptr, info.data.size_);
      write_port_cdc_.Finish(true, ErrorCode::INIT_ERR, info, 0);
      return;
    }

    auto need_write = info.data.size_ - written_;

    if (need_write > 0)
    {
      auto buffer = ep_data_in->GetBuffer();
      if (buffer.size_ < need_write)
      {
        need_write = buffer.size_;
      }

      write_port_cdc_.queue_data_->PopBatch(reinterpret_cast<uint8_t*>(buffer.addr_),
                                            need_write);
      ep_data_in->SetActiveLength(need_write);
      written_ += need_write;

      if (written_ >= info.data.size_)
      {
        write_port_cdc_.queue_info_->Pop(info);
        write_port_cdc_.Finish(true, ErrorCode::OK, info, info.data.size_);
        written_ -= info.data.size_;
        ASSERT(written_ == 0);
      }
    }
  }

 private:
  // 端口对象
  LibXR::ReadPort read_port_cdc_;    ///< 读取端口 / Read port
  LibXR::WritePort write_port_cdc_;  ///< 写入端口 / Write port

  // 状态标志
  bool need_write_zlp_ = false;  ///< 需要写入ZLP标志 / Need to write ZLP flag
  size_t written_ = 0;           ///< 已写入字节数 / Number of bytes written
};

}  // namespace LibXR::USB
