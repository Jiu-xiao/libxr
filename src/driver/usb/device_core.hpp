#pragma once
#include <cstring>

#include "core.hpp"
#include "descriptor.hpp"
#include "endpoint_pool.hpp"

namespace LibXR::USB
{
class DeviceCore
{
 public:
  DeviceCore(EndpointPool *ep_pool, USBSpec spec, Speed speed,
             DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
             uint16_t bcd, uint8_t num_configs = 1, uint8_t num_langs = 1)
      : endpoint_pool_(ep_pool),
        speed_(speed),
        config_max_num_(num_configs),
        config_desc_(new ConfigDescriptor *[num_configs]),
        device_desc_(spec, packet_size, vid, pid, bcd, num_configs),
        strings_(num_langs)
  {
    ASSERT(IsValidUSBCombination(spec, speed, packet_size));
  }

  static void OnEP0OutCompleteStatic(bool in_isr, DeviceCore *self,
                                     LibXR::ConstRawData &data)
  {
    self->OnEP0OutComplete(in_isr, data);
  }

  static void OnEP0InCompleteStatic(bool in_isr, DeviceCore *self,
                                    LibXR::ConstRawData &data)
  {
    self->OnEP0InComplete(in_isr, data);
  }

  void InitEndpoint0Handlers()
  {
    ep0_in_ = endpoint_pool_->GetEndpoint0In();
    ep0_out_ = endpoint_pool_->GetEndpoint0Out();

    ep0_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::CONTROL, 64});
    ep0_out_->Configure({Endpoint::Direction::OUT, Endpoint::Type::CONTROL, 64});

    ep0_in_->on_transfer_complete_ =
        LibXR::Callback<LibXR::ConstRawData &>::Create(OnEP0InCompleteStatic, this);
    ep0_out_->on_transfer_complete_ =
        LibXR::Callback<LibXR::ConstRawData &>::Create(OnEP0OutCompleteStatic, this);
  }

  static bool IsValidUSBCombination(USBSpec spec, Speed speed,
                                    DeviceDescriptor::PacketSize0 packet_size)
  {
    const uint8_t SIZE = static_cast<uint8_t>(packet_size);

    switch (speed)
    {
      case Speed::LOW:
        // USB 1.0/1.1 可允许 LOW，包长必须为 8
        return (spec == USBSpec::USB_1_0 || spec == USBSpec::USB_1_1) && SIZE == 8;

      case Speed::FULL:
        // USB 1.x/2.0 均可运行在 FULL
        if (!(spec >= USBSpec::USB_1_0 && spec <= USBSpec::USB_2_1))
        {
          return false;
        }
        return SIZE == 8 || SIZE == 16 || SIZE == 32 || SIZE == 64;

      case Speed::HIGH:
        // 只有 USB 2.0+ 才能跑 HIGH，包长必须是 64
        if (spec < USBSpec::USB_2_0)
        {
          return false;
        }
        return SIZE == 64;

      case Speed::SUPER:  // NOLINT
      case Speed::SUPER_PLUS:
        return false;

      default:
        return false;
    }
  }

  void OnEP0OutComplete(bool in_isr, LibXR::ConstRawData &data) {}

  void OnEP0InComplete(bool in_isr, LibXR::ConstRawData &data)
  {  // SET_ADDRESS写硬件在这里处理
    if (pending_address_ != 0xFF)
    {
      SetAddress(pending_address_);
      pending_address_ = 0xFF;  // 表示已完成
    }
    else if (write_remain_.size_ > 0)
    {
      ReportToEP0(write_remain_, ep0_in_->MaxPacketSize());
    }
    else if (need_write_zlp_)
    {
      // 如果需要发送ZLP，发送一个空数据包
      LibXR::ConstRawData zlp(nullptr, 0);
      ep0_in_->Write(zlp);
      need_write_zlp_ = false;  // 清除标志

      ReadZLP();
    }
  }

  void ReadZLP()
  {
    static LibXR::RawData zlp(nullptr, 0);  // NOLINT
    ep0_out_->Read(zlp);
  }

  void WriteZLP()
  {
    static LibXR::ConstRawData zlp(nullptr, 0);  // NOLINT
    ep0_out_->Write(zlp);
  }

  void ReportToEP0(LibXR::ConstRawData data, size_t packet_max_length,
                   size_t request_size = 0)
  {
    if (request_size > 0 && request_size < data.size_)
    {
      data.size_ = request_size;
    }

    if (data.size_ > packet_max_length)
    {
      write_remain_ = {reinterpret_cast<const uint8_t *>(data.addr_) + packet_max_length,
                       data.size_ - packet_max_length};
      need_write_zlp_ = false;

      data.size_ = packet_max_length;
    }
    else
    {
      write_remain_ = {nullptr, 0};  // 清空剩余数据
      need_write_zlp_ = (data.size_ == packet_max_length);
    }

    bool need_read_zlp = false;
    if (!need_write_zlp_ && write_remain_.size_ == 0)
    {
      need_read_zlp = true;
    }

    ASSERT(data.size_ > 0 && data.size_ <= packet_max_length);

    ep0_in_->Write(data);

    if (need_read_zlp)
    {
      ReadZLP();
    }
  }

  void OnSetupPacket(bool in_isr, const SetupPacket *setup)
  {
    // ----------- 解析 bmRequestType 三个字段 -----------
    RequestDirection direction =
        static_cast<RequestDirection>(setup->bmRequestType & REQ_DIRECTION_MASK);
    RequestType type = static_cast<RequestType>(setup->bmRequestType & REQ_TYPE_MASK);
    Recipient recipient =
        static_cast<Recipient>(setup->bmRequestType & REQ_RECIPIENT_MASK);

    if (ep0_in_->IsStalled())
    {
      ep0_in_->ClearStall();
    }

    if (ep0_out_->IsStalled())
    {
      ep0_out_->ClearStall();
    }

    // ----------- 非法类型保护（防御性编程，可选） -----------
    if (type == RequestType::RESERVED)
    {
      // STALL: 协议禁止处理保留类型
      // StallControlEndpoint(); // 你可以实现实际STALL
      return;
    }

    // ----------- 分发到具体请求类型 -----------
    switch (type)
    {
      case RequestType::STANDARD:
        HandleStandardRequest(in_isr, setup, direction, recipient);
        break;
      case RequestType::CLASS:
        HandleClassRequest(in_isr, setup, direction, recipient);
        break;
      case RequestType::VENDOR:
        HandleVendorRequest(in_isr, setup, direction, recipient);
        break;
      default:
        // 理论不会走到这里，保险起见直接STALL
        // StallControlEndpoint();
        break;
    }
  }

  // =================== 分发实现模板 ===================

  void HandleStandardRequest(bool in_isr, const SetupPacket *&setup,
                             RequestDirection direction, Recipient recipient)
  {
    UNUSED(in_isr);
    UNUSED(direction);
    UNUSED(recipient);
    // 根据 recipient 处理不同目标的请求
    StandardRequest req = static_cast<StandardRequest>(setup->bRequest);

    switch (req)
    {
      case StandardRequest::GET_STATUS:
        // 设备/接口/端点的状态请求
        HandleGetStatus(setup, recipient);
        break;
      case StandardRequest::CLEAR_FEATURE:
        // 解除某个特性（如端点的HALT）
        HandleClearFeature(setup, recipient);
        break;
      case StandardRequest::SET_FEATURE:
        // 设置特性（如Remote Wakeup/端点STALL）
        HandleSetFeature(setup, recipient);
        break;
      case StandardRequest::SET_ADDRESS:
        // 设置USB地址，status阶段后生效
        HandleSetAddress(setup->wValue);
        break;
      case StandardRequest::GET_DESCRIPTOR:
        // 返回设备/配置/字符串等描述符
        HandleGetDescriptor(setup);
        break;
      case StandardRequest::SET_DESCRIPTOR:
        // 很少用，可选实现
        break;
      case StandardRequest::GET_CONFIGURATION:
        // 返回当前配置
        HandleGetConfiguration();
        break;
      case StandardRequest::SET_CONFIGURATION:
        // 设置当前配置（切换Config描述符索引）
        HandleSetConfiguration(setup->wValue);
        break;
      case StandardRequest::GET_INTERFACE:
        // 获取接口AltSetting
        break;
      case StandardRequest::SET_INTERFACE:
        // 设置接口AltSetting
        break;
      case StandardRequest::SYNCH_FRAME:
        // 一般仅用于同步端点
        break;
      default:
        // 未知请求，直接STALL
        StallControlEndpoint();
        break;
    }
  }

  void HandleGetStatus(const SetupPacket *setup, Recipient recipient)
  {
    uint16_t status = 0;
    switch (recipient)
    {
      case Recipient::DEVICE:
        // 可根据你的实际实现返回bit0/bit1
        // 一般总线供电且不支持远程唤醒
        status = 0x0000;
        break;
      case Recipient::INTERFACE:
        status = 0x0000;  // 必须为0
        break;
      case Recipient::ENDPOINT:
      {
        uint8_t ep_addr = setup->wIndex & 0xFF;
        Endpoint *ep = nullptr;
        endpoint_pool_->GetEndpoint(ep_addr, ep);
        if (ep && ep->GetState() == Endpoint::State::STALLED)
        {
          status = 0x0001;
        }
        break;
      }
      default:
        status = 0x0000;
    }
    LibXR::ConstRawData data(&status, 2);
    ReportToEP0(data, ep0_in_->MaxPacketSize(), setup->wLength);
  }

  void HandleClearFeature(const SetupPacket *setup, Recipient recipient)
  {
    switch (recipient)
    {
      case Recipient::ENDPOINT:
      {
        // 只允许清除 ENDPOINT_HALT 特性（wValue==0）
        if (setup->wValue == 0)  // 0 = ENDPOINT_HALT
        {
          uint8_t ep_addr = setup->wIndex & 0xFF;  // 端点号（低7位=EP号, 高位=方向）
          // 清除端点STALL
          Endpoint *ep = nullptr;
          endpoint_pool_->GetEndpoint(ep_addr, ep);
          if (ep)
          {
            ep->ClearStall();
            // 状态阶段：回复ZLP
            LibXR::ConstRawData zlp(nullptr, 0);
            ep0_in_->Write(zlp);
          }
          else
          {
            StallControlEndpoint();  // 不存在端点，STALL
          }
        }
        else
        {
          StallControlEndpoint();  // 非标准特性，不支持
        }
        break;
      }
      case Recipient::DEVICE:
        // 可选：处理Remote Wakeup等设备级特性
        // 目前大部分USB设备不支持，直接ACK即可
        if (setup->wValue == 1)  // 1 = DEVICE_REMOTE_WAKEUP
        {
          // 你的remote_wakeup标志位可以在这里处理
          // TODO:
          // 状态阶段回复ZLP
          LibXR::ConstRawData zlp(nullptr, 0);
          ep0_in_->Write(zlp);
        }
        else
        {
          StallControlEndpoint();
        }
        break;
      case Recipient::INTERFACE:
        // 标准协议: 不支持INTERFACE级别特性
        StallControlEndpoint();
        break;
      default:
        StallControlEndpoint();
        break;
    }
  }

  void HandleSetFeature(const SetupPacket *setup, Recipient recipient)
  {
    switch (recipient)
    {
      case Recipient::ENDPOINT:
      {
        if (setup->wValue == 0)  // 0 = ENDPOINT_HALT
        {
          uint8_t ep_addr = setup->wIndex & 0xFF;
          // 设置端点STALL
          Endpoint *ep = nullptr;
          endpoint_pool_->GetEndpoint(ep_addr, ep);
          if (ep)
          {
            ep->Stall();  // 设置端点STALL
            // 状态阶段回复ZLP
            LibXR::ConstRawData zlp(nullptr, 0);
            ep0_in_->Write(zlp);
          }
          else
          {
            StallControlEndpoint();
          }
        }
        else
        {
          StallControlEndpoint();
        }
        break;
      }
      case Recipient::DEVICE:
        if (setup->wValue == 1)  // 1 = DEVICE_REMOTE_WAKEUP
        {
          // 支持Remote Wakeup的话，这里enable
          // TODO:
          LibXR::ConstRawData zlp(nullptr, 0);
          ep0_in_->Write(zlp);
        }
        else
        {
          StallControlEndpoint();
        }
        break;
      case Recipient::INTERFACE:
        // 标准协议: 不支持INTERFACE级别特性
        StallControlEndpoint();
        break;
      default:
        StallControlEndpoint();
        break;
    }
  }

  void HandleGetDescriptor(const SetupPacket *setup)
  {
    uint8_t desc_type = (setup->wValue >> 8) & 0xFF;
    uint8_t desc_idx = (setup->wValue) & 0xFF;
    const void *ptr = nullptr;
    size_t len = 0;
    switch (desc_type)
    {
      case 0x01:  // DEVICE
        ptr = &device_desc_;
        len = device_desc_.DEVICE_DESC_LENGTH;
        break;
      case 0x02:  // CONFIGURATION
        if (current_config_index_ < config_num_ && config_desc_[current_config_index_])
        {
          auto desc = config_desc_[current_config_index_];
          ptr = desc->GetData().addr_;
          len = desc->GetData().size_;
        }
        break;
      case 0x03:  // STRING
      {
        uint8_t string_idx = desc_idx;
        uint16_t lang = setup->wIndex;
        if (string_idx == 0)
        {
          ptr = strings_.GetLangIDData().addr_;
          len = strings_.GetLangIDData().size_;
        }
        else
        {
          ErrorCode ec = strings_.GenerateString(
              static_cast<DescriptorStrings::Index>(string_idx), lang);
          if (ec != ErrorCode::OK)
          {
            StallControlEndpoint();
            return;
          }
          ptr = strings_.GetData().addr_;
          len = strings_.GetData().size_;
        }
        break;
      }
      case 0x06:  // Device Qualifier (USB 2.0+)
                  // TODO:
        StallControlEndpoint();
        return;
      default:
        StallControlEndpoint();
        return;
    }

    LibXR::ConstRawData data(ptr, len);

    ReportToEP0(data, ep0_in_->MaxPacketSize(), setup->wLength);
  }

  void HandleSetAddress(uint16_t address)
  {
    // USB标准：status阶段后才能写硬件地址
    // pending_address_ = static_cast<uint8_t>(address & 0x7F);
    LibXR::ConstRawData zlp(nullptr, 0);
    ep0_in_->Write(zlp);
    SetAddress(static_cast<uint8_t>(address & 0x7F));
  }

  void HandleSetConfiguration(uint16_t value)
  {
    if (value == 0)
    {
      current_config_index_ = 0;
    }
    else if (value <= config_num_)
    {
      current_config_index_ = value - 1;
    }
    else
    {
      StallControlEndpoint();
      return;
    }
    // ACK status阶段
    LibXR::ConstRawData zlp(nullptr, 0);
    endpoint_pool_->GetEndpoint0In()->Write(zlp);
  }

  void HandleGetConfiguration()
  {
    uint8_t cfg = (current_config_index_ < config_num_) ? (current_config_index_ + 1) : 0;
    LibXR::ConstRawData data(&cfg, 1);
    endpoint_pool_->GetEndpoint0In()->Write(data);
  }

  // ========== 其它请求和错误处理 ==========
  void StallControlEndpoint()
  {
    // ReadZLP();
    endpoint_pool_->GetEndpoint0Out()->Stall();
    endpoint_pool_->GetEndpoint0In()->Stall();
  }

  void ClearControlEndpointStall()
  {
    endpoint_pool_->GetEndpoint0Out()->ClearStall();
    endpoint_pool_->GetEndpoint0In()->ClearStall();
  }

  void HandleClassRequest(bool in_isr, const SetupPacket *setup,
                          RequestDirection direction, Recipient recipient)
  {
    // 根据具体Class（如CDC/HID/MSD）进一步分发
    // if (CDC类) HandleCDCRequest(setup); ...
  }

  void HandleVendorRequest(bool in_isr, const SetupPacket *&setup,
                           RequestDirection direction, Recipient recipient)
  {
    // 处理厂商自定义请求
    // if (setup->bRequest == VENDOR_CMD_XXX) ...
  }

  ErrorCode AddConfigDescriptor(ConfigDescriptor *desc)
  {
    if (config_num_ >= config_max_num_)
    {
      return ErrorCode::FULL;
    }

    ASSERT(desc != nullptr);
    config_desc_[config_num_] = desc;
    config_num_++;
    return ErrorCode::OK;
  }

  ErrorCode GenerateConfigDescriptor()
  {
    if (current_config_index_ >= config_num_)
    {
      return ErrorCode::NOT_FOUND;
    }

    InitEndpoint0Handlers();

    strings_.Setup();

    auto desc = config_desc_[current_config_index_];
    ASSERT(desc != nullptr);

    ErrorCode ans = desc->Generate();
    if (ans != ErrorCode::OK)
    {
      return ans;
    }

    return ErrorCode::OK;
  }

  size_t GetCurrentConfigIndex() const { return current_config_index_; }

  size_t GetConfigNum() const { return config_num_; }

  void SetCurrentConfigIndex(size_t index) { current_config_index_ = index; }

  ErrorCode AddLanguage(DescriptorStrings::Language lang_id,
                        const char *manufacturer_id = "N/A",
                        const char *product_id = "N/A", const char *serial_id = "N/A")
  {
    return strings_.AddLanguage(lang_id, manufacturer_id, product_id, serial_id);
  }

  virtual ErrorCode SetAddress(uint8_t address) = 0;

 private:
  EndpointPool *endpoint_pool_ = nullptr;
  Endpoint *ep0_in_ = nullptr;
  Endpoint *ep0_out_ = nullptr;
  Speed speed_ = Speed::FULL;
  size_t config_max_num_ = 1;
  ConfigDescriptor **config_desc_ = nullptr;

  ConstRawData write_remain_;
  bool need_write_zlp_ = false;

  DeviceDescriptor device_desc_;
  DescriptorStrings strings_;

  size_t config_num_ = 0;
  size_t current_config_index_ = 0;
  uint8_t pending_address_ = 0xFF;
};
}  // namespace LibXR::USB
