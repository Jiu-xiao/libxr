#pragma once
#include <cstring>

#include "desc_cfg.hpp"
#include "dev_core.hpp"
#include "uart.hpp"

namespace LibXR::USB
{

class CDC : public DeviceClass, public LibXR::UART
{
  enum class DescriptorSubtype : uint8_t
  {
    HEADER = 0x00,           // 必须（所有CDC功能块）
    CALL_MANAGEMENT = 0x01,  // 调用管理（管理数据和命令接口关系）
    ACM = 0x02,              // Abstract Control Management（ACM，虚拟串口用）
    UNION = 0x06,            // 必须（接口归组）
  };

  enum class Class : uint8_t
  {
    COMM = 0x02,
    DATA = 0x0A
  };

  enum class Protocol : uint8_t
  {
    AT_COMMAND = 0x01,
  };

  enum class Subclass : uint8_t
  {
    NONE = 0x00,                       // 无子类
    DIRECT_LINE_CONTROL_MODEL = 0x01,  // Direct Line Control Model
    ABSTRACT_CONTROL_MODEL = 0x02,     // Abstract Control Model（CDC-ACM用）
  };

  enum class ClassRequest : uint8_t
  {
    SET_LINE_CODING = 0x20,         // 设置串口参数（波特率/校验/停止位/数据位）
    GET_LINE_CODING = 0x21,         // 获取当前串口参数
    SET_CONTROL_LINE_STATE = 0x22,  // 设置DTR/RTS
    SEND_BREAK = 0x23               // 发送Break信号
  };

  enum class CDCNotification : uint8_t
  {
    NETWORK_CONNECTION = 0x00,   // CDC-ECM等网络设备: 网络连接状态变更
    RESPONSE_AVAILABLE = 0x01,   // 调制解调器/AT等: 有可读响应
    AUX_JACK_HOOK_STATE = 0x08,  // 电话相关（罕见）
    RING_DETECT = 0x09,          // 电话来电检测
    SERIAL_STATE = 0x20,         // ★串口状态通知（CDC-ACM虚拟串口必备，最常用）
  };

#pragma pack(push, 1)
  struct CDCLineCoding
  {
    uint32_t dwDTERate;   // 波特率（小端），如 115200
    uint8_t bCharFormat;  // 停止位: 0=1位, 1=1.5位, 2=2位
    uint8_t bParityType;  // 校验: 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    uint8_t bDataBits;    // 数据位: 5,6,7,8,16
  };

  struct SerialStateNotification
  {
    uint8_t bmRequestType;  // 固定0xA1（Device-to-Host, Class, Interface）
    uint8_t bNotification;  // 0x20 (SERIAL_STATE)
    uint16_t wValue;        // 0
    uint16_t wIndex;        // 通信接口号（一般是你的COMM_INTERFACE编号，比如0）
    uint16_t wLength;       // 2
    uint16_t serialState;   // 具体状态，按位与CDC规范对应
  };
#pragma pack(pop)

  static_assert(sizeof(CDCLineCoding) == 7, "LineCoding结构必须7字节");

  static constexpr uint16_t CDC_CONTROL_LINE_DTR = 0x01;
  static constexpr uint16_t CDC_CONTROL_LINE_RTS = 0x02;

 public:
  using LibXR::UART::Read;
  using LibXR::UART::read_port_;
  using LibXR::UART::Write;
  using LibXR::UART::write_port_;

  CDC(size_t rx_buffer_size = 128, size_t tx_buffer_size = 128, size_t tx_queue_size = 5,
      Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : LibXR::UART(&read_port_cdc_, &write_port_cdc_),
        read_port_cdc_(rx_buffer_size),
        write_port_cdc_(tx_queue_size, tx_buffer_size),
        data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        comm_ep_num_(comm_ep_num)
  {
    read_port_cdc_ = ReadFun;    // NOLINT
    write_port_cdc_ = WriteFun;  // NOLINT
  }

  void Init(EndpointPool& endpoint_pool, size_t start_itf_num) override
  {
    control_line_state_ = 0;
    auto ans = endpoint_pool.Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ans = endpoint_pool.Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ans = endpoint_pool.Get(ep_comm_in_, Endpoint::Direction::IN, comm_ep_num_);
    ASSERT(ans == ErrorCode::OK);

    ep_data_in_->Configure(
        {Endpoint::Direction::IN, Endpoint::Type::BULK, UINT16_MAX, true});
    ep_data_out_->Configure(
        {Endpoint::Direction::OUT, Endpoint::Type::BULK, UINT16_MAX, true});
    ep_comm_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::INTERRUPT, 8});

    // === 填充描述符 ===
    static constexpr uint8_t COMM_INTERFACE = 0;
    static constexpr uint8_t DATA_INTERFACE = 1;

    // IAD 描述符
    desc_block_.iad = {8,
                       static_cast<uint8_t>(DescriptorType::IAD),
                       COMM_INTERFACE,
                       2,
                       static_cast<uint8_t>(Class::COMM),
                       static_cast<uint8_t>(Subclass::ABSTRACT_CONTROL_MODEL),
                       static_cast<uint8_t>(Protocol::AT_COMMAND),
                       0};

    // Comm Interface 描述符
    desc_block_.comm_intf = {9,
                             static_cast<uint8_t>(DescriptorType::INTERFACE),
                             COMM_INTERFACE,
                             0,
                             1,
                             static_cast<uint8_t>(Class::COMM),
                             static_cast<uint8_t>(Subclass::ABSTRACT_CONTROL_MODEL),
                             static_cast<uint8_t>(Protocol::AT_COMMAND),
                             0};

    // Class-specific: Header
    desc_block_.cdc_header = {5, DescriptorType::CS_INTERFACE, DescriptorSubtype::HEADER,
                              0x0110};

    // Call Management
    desc_block_.cdc_callmgmt = {5, DescriptorType::CS_INTERFACE,
                                DescriptorSubtype::CALL_MANAGEMENT,
                                0x00,  // 无 DTE 管理能力
                                DATA_INTERFACE};

    // Abstract Control Model
    desc_block_.cdc_acm = {
        4, DescriptorType::CS_INTERFACE, DescriptorSubtype::ACM,
        0x02};  // 支持 Set_Line_Coding, Get_Line_Coding, Set_Control_Line_State

    // Union Functional
    desc_block_.cdc_union = {5, DescriptorType::CS_INTERFACE, DescriptorSubtype::UNION,
                             COMM_INTERFACE, DATA_INTERFACE};

    // Data Interface 描述符
    desc_block_.data_intf = {9,
                             static_cast<uint8_t>(DescriptorType::INTERFACE),
                             DATA_INTERFACE,
                             0,
                             2,
                             static_cast<uint8_t>(Class::DATA),
                             0x00,
                             0x00,
                             0};

    // Data OUT Endpoint
    desc_block_.data_ep_out = {7,
                               static_cast<uint8_t>(DescriptorType::ENDPOINT),
                               ep_data_out_->GetAddress(),
                               static_cast<uint8_t>(Endpoint::Type::BULK),
                               ep_data_out_->MaxPacketSize(),
                               0};

    // Data IN Endpoint
    desc_block_.data_ep_in = {7,
                              static_cast<uint8_t>(DescriptorType::ENDPOINT),
                              static_cast<uint8_t>(ep_data_in_->GetAddress()),
                              static_cast<uint8_t>(Endpoint::Type::BULK),
                              ep_data_in_->MaxPacketSize(),
                              0};

    desc_block_.comm_ep = {
        7,
        static_cast<uint8_t>(DescriptorType::ENDPOINT),
        static_cast<uint8_t>(ep_comm_in_->GetAddress() | 0x80),  // IN 端点
        static_cast<uint8_t>(Endpoint::Type::INTERRUPT),
        8,    // 推荐：8字节中断包
        0x10  // 16ms polling interval
    };

    itf_comm_in_num_ = start_itf_num;

    // 设置最终原始数据指针
    data_ = RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)};

    ep_data_out_->SetOnTransferCompleteCallback(on_data_out_complete_cb_);
    ep_data_in_->SetOnTransferCompleteCallback(on_data_in_complete_cb_);

    inited_ = true;

    ep_data_out_->TransferBulk(ep_data_out_->MaxTransferSize());
  }

  static void OnDataOutCompleteStatic(bool in_isr, CDC* self, ConstRawData& data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataOutComplete(in_isr, data);
  }

  static void OnDataInCompleteStatic(bool in_isr, CDC* self, ConstRawData& data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataInComplete(in_isr, data);
  }

  void OnDataOutComplete(bool in_isr, ConstRawData& data)
  {
    ep_data_out_->TransferBulk(ep_data_out_->MaxTransferSize());
    if (data.size_ > 0)
    {
      read_port_cdc_.queue_data_->PushBatch(reinterpret_cast<const uint8_t*>(data.addr_),
                                            data.size_);
      read_port_cdc_.ProcessPendingReads(in_isr);
    }
  }

  void OnDataInComplete(bool in_isr, ConstRawData& data)
  {
    UNUSED(in_isr);
    UNUSED(data);
    size_t pending_len = ep_data_in_->GetActiveLength();

    if (pending_len == 0)
    {
      return;
    }

    auto ans = ep_data_in_->TransferBulk(pending_len);

    WriteInfoBlock info;
    if (write_port_cdc_.queue_info_->Pop(info) != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }

    write_port_cdc_.Finish(in_isr, ans, info, pending_len);

    if (write_port_cdc_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return;
    }

    auto buffer = ep_data_in_->GetBuffer();

    if (info.data.size_ > buffer.size_)
    {
      write_port_cdc_.queue_info_->Pop();
      write_port_cdc_.queue_data_->PopBatch(nullptr, info.data.size_);
      write_port_cdc_.Finish(in_isr, ErrorCode::NO_BUFF, info, 0);
      return;
    }

    ans = write_port_cdc_.queue_data_->PopBatch(reinterpret_cast<uint8_t*>(buffer.addr_),
                                                info.data.size_);

    ASSERT(ans == ErrorCode::OK);

    ep_data_in_->SetActiveLength(info.data.size_);
  }

  static ErrorCode WriteFun(WritePort& port)
  {
    CDC* cdc = CONTAINER_OF(&port, CDC, write_port_cdc_);

    if (!cdc->inited_ || !cdc->IsDtrSet() || cdc->ep_comm_in_busy_)
    {
      cdc->ep_data_in_->SetActiveLength(0);
      WriteInfoBlock info;
      if (port.queue_info_->Pop(info) == ErrorCode::OK)
      {
        port.queue_data_->PopBatch(nullptr, info.data.size_);
        port.Finish(false, ErrorCode::NO_BUFF, info, 0);
      }
      port.Reset();
      return ErrorCode::FAILED;
    }

    auto buffer = cdc->ep_data_in_->GetBuffer();

    if (cdc->ep_data_in_->GetActiveLength() > 0)
    {
      return ErrorCode::FAILED;
    }

    WriteInfoBlock info;

    if (port.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    if (info.data.size_ > buffer.size_)
    {
      port.queue_info_->Pop();
      port.queue_data_->PopBatch(nullptr, info.data.size_);
      port.Finish(false, ErrorCode::NO_BUFF, info, 0);
      return ErrorCode::NO_BUFF;
    }

    if (port.queue_data_->PopBatch(reinterpret_cast<uint8_t*>(buffer.addr_),
                                   info.data.size_) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    cdc->ep_data_in_->SetActiveLength(info.data.size_);

    if (cdc->ep_data_in_->GetState() == Endpoint::State::IDLE &&
        cdc->ep_data_in_->GetActiveLength() != 0)
    {
      /* Can Write Now */
    }
    else
    {
      return ErrorCode::FAILED;
    }

    auto ans = port.queue_info_->Pop(info);

    ASSERT(ans == ErrorCode::OK);

    ans = cdc->ep_data_in_->TransferBulk(info.data.size_);

    if (ans != ErrorCode::OK)
    {
      port.Finish(false, ErrorCode::FAILED, info, 0);
      return ErrorCode::FAILED;
    }

    return ErrorCode::OK;
  }

  static ErrorCode ReadFun(ReadPort& port)
  {
    UNUSED(port);

    return ErrorCode::EMPTY;
  }

  void Deinit(EndpointPool& endpoint_pool) override
  {
    inited_ = false;
    control_line_state_ = 0;
    ep_data_in_->Close();
    ep_data_out_->Close();
    ep_comm_in_->Close();
    endpoint_pool.Release(ep_data_in_);
    endpoint_pool.Release(ep_data_out_);
    endpoint_pool.Release(ep_comm_in_);
    ep_data_in_ = nullptr;
    ep_data_out_ = nullptr;
    ep_comm_in_ = nullptr;
  }

  size_t GetInterfaceNum() override { return 2; }

  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }

  ErrorCode OnClassRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                           uint16_t wLength, DeviceClass::RequestResult& result) override
  {
    UNUSED(in_isr);
    switch (static_cast<ClassRequest>(bRequest))
    {
      case ClassRequest::SET_LINE_CODING:
        // 主机OUT数据阶段后发送7字节参数，进入OnClassData处理
        if (wLength != sizeof(line_coding_))
        {
          return ErrorCode::ARG_ERR;
        }
        result.read_data =
            RawData{reinterpret_cast<uint8_t*>(&line_coding_), sizeof(line_coding_)};
        return ErrorCode::OK;

      case ClassRequest::GET_LINE_CODING:
        // 主机IN数据阶段，直接返回line_coding_给主机
        if (wLength != sizeof(line_coding_))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data = ConstRawData{reinterpret_cast<const uint8_t*>(&line_coding_),
                                         sizeof(line_coding_)};
        SendSerialState();
        return ErrorCode::OK;

      case ClassRequest::SET_CONTROL_LINE_STATE:
        // 主机OUT无数据，bRequest.wValue即DTR/RTS，标准流程数据在setup里传递
        control_line_state_ = wValue;
        // TODO: user callback
        result.write_zlp = true;
        SendSerialState();
        return ErrorCode::OK;

      case ClassRequest::SEND_BREAK:
        // 一般忽略或立即OK
        return ErrorCode::OK;

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);

    switch (static_cast<ClassRequest>(bRequest))
    {
      case ClassRequest::SET_LINE_CODING:
        // TODO: user callback
        return ErrorCode::OK;
      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header) override
  {
    /* 复合接口设备不需要描述符 */
    ASSERT(false);
    UNUSED(header);
    return ErrorCode::NOT_SUPPORT;
  }

  ErrorCode SendSerialState()
  {
    ep_comm_in_busy_ = true;
    auto buffer = ep_comm_in_->GetBuffer();
    SerialStateNotification* notification =
        reinterpret_cast<SerialStateNotification*>(buffer.addr_);
    notification->wIndex = itf_comm_in_num_;

    // 只要DTR为1，通常设置 DCD/DSR 有效
    if (IsDtrSet())
    {
      notification->serialState = 0x03;  // RxCarrier + TxCarrier
    }
    else
    {
      notification->serialState = 0x00;  // 无信号
    }

    notification->bmRequestType = 0xA1;
    notification->bNotification = 0x20;
    notification->wValue = 0;
    notification->wLength = 2;

    ep_comm_in_busy_ = false;

    return ErrorCode::OK;
  }

  ErrorCode SetConfig(UART::Configuration cfg) override
  {
    switch (cfg.stop_bits)
    {
      case 1:
        line_coding_.bCharFormat = 0;
        break;
      case 2:
        line_coding_.bCharFormat = 2;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    switch (cfg.parity)
    {
      case UART::Parity::NO_PARITY:
        line_coding_.bParityType = 0;
        break;
      case UART::Parity::EVEN:
        line_coding_.bParityType = 1;
        break;
      case UART::Parity::ODD:
        line_coding_.bParityType = 2;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    switch (cfg.data_bits)
    {
      case 5:
        line_coding_.bDataBits = 5;
        break;
      case 6:
        line_coding_.bDataBits = 6;
        break;
      case 7:
        line_coding_.bDataBits = 7;
        break;
      case 8:
        line_coding_.bDataBits = 8;
        break;
      case 16:
        line_coding_.bDataBits = 16;
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    line_coding_.dwDTERate = cfg.baudrate;

    SendSerialState();

    return ErrorCode::OK;
  }

  bool IsDtrSet() const { return (control_line_state_ & CDC_CONTROL_LINE_DTR) != 0; }

  bool IsRtsSet() const { return (control_line_state_ & CDC_CONTROL_LINE_RTS) != 0; }

 private:
#pragma pack(push, 1)
  struct CDCDescBlock
  {
    // 1. IAD
    IADDescriptor iad;
    // 2. Comm Interface
    InterfaceDescriptor comm_intf;
    // 3. Class-specific descriptors
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      DescriptorSubtype bDescriptorSubtype = DescriptorSubtype::HEADER;
      uint16_t bcdCDC = 0x0110;
    } cdc_header;
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      DescriptorSubtype bDescriptorSubtype = DescriptorSubtype::CALL_MANAGEMENT;
      uint8_t bmCapabilities = 0x00;
      uint8_t bDataInterface;
    } cdc_callmgmt;
    struct
    {
      uint8_t bFunctionLength = 4;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      DescriptorSubtype bDescriptorSubtype = DescriptorSubtype::ACM;
      uint8_t bmCapabilities = 0x02;
    } cdc_acm;
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      DescriptorSubtype bDescriptorSubtype = DescriptorSubtype::UNION;
      uint8_t bMasterInterface;
      uint8_t bSlaveInterface0;
    } cdc_union;
    EndpointDescriptor comm_ep;

    // 4. Data Interface
    InterfaceDescriptor data_intf;

    // 5. Data Endpoints
    EndpointDescriptor data_ep_out;
    EndpointDescriptor data_ep_in;
  } desc_block_;
#pragma pack(pop)

  LibXR::ReadPort read_port_cdc_;
  LibXR::WritePort write_port_cdc_;

  Endpoint::EPNumber data_in_ep_num_;
  Endpoint::EPNumber data_out_ep_num_;
  Endpoint::EPNumber comm_ep_num_;

  Endpoint* ep_data_in_ = nullptr;
  Endpoint* ep_data_out_ = nullptr;
  Endpoint* ep_comm_in_ = nullptr;

  LibXR::Callback<LibXR::ConstRawData&> on_data_out_complete_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData&> on_data_in_complete_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataInCompleteStatic, this);

  bool inited_ = false;
  bool ep_comm_in_busy_ = false;

  size_t itf_comm_in_num_;
  CDCLineCoding line_coding_ = {115200, 0, 0, 8};  // 默认串口参数
  uint16_t control_line_state_ = 0;                // DTR/RTS 状态
};

}  // namespace LibXR::USB
