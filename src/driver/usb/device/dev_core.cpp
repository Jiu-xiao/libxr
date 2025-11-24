#include "dev_core.hpp"

using namespace LibXR::USB;

DeviceCore::DeviceCore(
    EndpointPool &ep_pool, USBSpec spec, Speed speed,
    DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
    const std::initializer_list<const DescriptorStrings::LanguagePack *> &lang_list,
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem *>>
        &configs,
    ConstRawData uid)
    : config_desc_(ep_pool, configs),
      device_desc_(spec, packet_size, vid, pid, bcd, config_desc_.GetConfigNum()),
      strings_(lang_list, reinterpret_cast<const uint8_t*>(uid.addr_), uid.size_),
      endpoint_({ep_pool, nullptr, nullptr, {}, {}}),
      state_({false, speed, Context::UNKNOW, Context::UNKNOW, 0xFF, nullptr, false})
{
  ASSERT(IsValidUSBCombination(spec, speed, packet_size));

  endpoint_.ep0_in_cb =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnEP0InCompleteStatic, this);

  endpoint_.ep0_out_cb =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnEP0OutCompleteStatic, this);
}

void DeviceCore::OnEP0OutCompleteStatic(bool in_isr, DeviceCore *self,
                                        LibXR::ConstRawData &data)
{
  self->OnEP0OutComplete(in_isr, data);
}

void DeviceCore::OnEP0InCompleteStatic(bool in_isr, DeviceCore *self,
                                       LibXR::ConstRawData &data)
{
  self->OnEP0InComplete(in_isr, data);
}

bool DeviceCore::IsValidUSBCombination(USBSpec spec, Speed speed,
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

void DeviceCore::ReadZLP(Context context)
{
  state_.out0 = context;
  endpoint_.out0->TransferZLP();
}

void DeviceCore::WriteZLP(Context context)
{
  state_.in0 = context;
  endpoint_.in0->TransferZLP();
}

void DeviceCore::Init()
{
  endpoint_.in0 = endpoint_.pool.GetEndpoint0In();
  endpoint_.out0 = endpoint_.pool.GetEndpoint0Out();

  endpoint_.in0->Configure({Endpoint::Direction::IN, Endpoint::Type::CONTROL, 64});
  endpoint_.out0->Configure({Endpoint::Direction::OUT, Endpoint::Type::CONTROL, 64});

  endpoint_.in0->SetOnTransferCompleteCallback(endpoint_.ep0_in_cb);
  endpoint_.out0->SetOnTransferCompleteCallback(endpoint_.ep0_out_cb);

  config_desc_.AssignEndpoints();

  state_.inited = true;
}

void DeviceCore::Deinit()
{
  state_.inited = false;
  config_desc_.ReleaseEndpoints();
  endpoint_.in0->Close();
  endpoint_.out0->Close();
}

void DeviceCore::OnEP0OutComplete(bool in_isr, LibXR::ConstRawData &data)
{
  if (!state_.inited)
  {
    return;
  }
  auto status = this->state_.out0;
  state_.out0 = Context::UNKNOW;

  switch (status)
  {
    case Context::ZLP:
    case Context::STATUS_OUT:
      break;
    case Context::DATA_OUT:
      if (data.size_ > 0)
      {
        LibXR::Memory::FastCopy(state_.out0_buffer, data.addr_, data.size_);
      }

      if (state_.read_remain.size_ > 0)
      {
        state_.out0_buffer += data.size_;
        DevReadEP0Data(state_.read_remain, endpoint_.out0->MaxTransferSize());
      }
      else if (class_req_.read)
      {
        class_req_.read = false;
        class_req_.class_ptr->OnClassData(in_isr, class_req_.b_request, data);
        WriteZLP();
      }
      else
      {
        WriteZLP();
      }
      break;
    default:
      StallControlEndpoint();
      break;
  }
}

void DeviceCore::OnEP0InComplete(bool in_isr, LibXR::ConstRawData &data)
{
  if (!state_.inited)
  {
    return;
  }
  UNUSED(in_isr);
  UNUSED(data);

  auto status = state_.in0;

  state_.in0 = Context::UNKNOW;

  switch (status)
  {
    case Context::ZLP:
      break;
    case Context::STATUS_IN:
      if (state_.pending_addr != 0xFF)
      {
        SetAddress(state_.pending_addr, Context::STATUS_IN);
        state_.pending_addr = 0xFF;
      }
      break;
    case Context::DATA_IN:
      if (state_.write_remain.size_ > 0)
      {
        DevWriteEP0Data(state_.write_remain, endpoint_.in0->MaxTransferSize());
      }
      else if (state_.need_write_zlp)
      {
        state_.need_write_zlp = false;
        ReadZLP();
        WriteZLP();
      }
      else if (class_req_.write)
      {
        class_req_.write = false;
        class_req_.class_ptr->OnClassData(in_isr, class_req_.b_request, data);
      }
      break;
    default:
      StallControlEndpoint();
      break;
  }
}

void DeviceCore::DevWriteEP0Data(LibXR::ConstRawData data, size_t packet_max_length,
                                 size_t request_size)
{
  state_.in0 = Context::DATA_IN;

  // 限制最大传输长度
  if (request_size > 0 && request_size < data.size_)
  {
    data.size_ = request_size;
  }

  // 数据长度为 0，直接 STALL（协议层面不允许）
  if (data.size_ == 0 || data.size_ > 0xFFFF)
  {
    StallControlEndpoint();
    return;
  }

  // 判断是否需要拆包或发送 ZLP
  bool has_more = data.size_ > packet_max_length;

  // 拆分数据包
  if (has_more)
  {
    state_.write_remain = {
        reinterpret_cast<const uint8_t *>(data.addr_) + packet_max_length,
        data.size_ - packet_max_length};
    data.size_ = packet_max_length;
    state_.need_write_zlp = false;
  }
  else
  {
    state_.write_remain = {nullptr, 0};
    state_.need_write_zlp = data.size_ % endpoint_.in0->MaxPacketSize() == 0;
  }

  auto buffer = endpoint_.in0->GetBuffer();
  ASSERT(buffer.size_ >= data.size_);
  LibXR::Memory::FastCopy(buffer.addr_, data.addr_, data.size_);

  // 如果一次就能结束，并且不需要 ZLP，直接准备状态阶段 OUT
  if (!has_more && !state_.need_write_zlp)
  {
    ReadZLP();
  }

  endpoint_.in0->Transfer(data.size_);
}

void DeviceCore::DevReadEP0Data(LibXR::RawData data, size_t packet_max_length)
{
  state_.out0 = Context::DATA_OUT;

  // 限制最大接收长度
  if (data.size_ == 0 || data.size_ > 0xFFFF)
  {
    StallControlEndpoint();
    return;
  }

  // 数据长度 <= 一个包长，直接收一次即可
  if (data.size_ <= packet_max_length)
  {
    state_.read_remain = {nullptr, 0};  // 标记本次已收完
  }
  else
  {
    // 数据需要多包接收
    state_.read_remain = {reinterpret_cast<uint8_t *>(data.addr_) + packet_max_length,
                          data.size_ - packet_max_length};
    data.size_ = packet_max_length;
  }

  state_.out0_buffer = reinterpret_cast<uint8_t *>(data.addr_);
  endpoint_.out0->Transfer(data.size_);  // 一次性收满，HAL/底层自动搞定多包
}

void DeviceCore::OnSetupPacket(bool in_isr, const SetupPacket *setup)
{
  if (!state_.inited)
  {
    return;
  }
  RequestDirection direction =
      static_cast<RequestDirection>(setup->bmRequestType & REQ_DIRECTION_MASK);
  RequestType type = static_cast<RequestType>(setup->bmRequestType & REQ_TYPE_MASK);
  Recipient recipient = static_cast<Recipient>(setup->bmRequestType & REQ_RECIPIENT_MASK);

  if (endpoint_.in0->IsStalled())
  {
    endpoint_.in0->ClearStall();
  }

  if (endpoint_.out0->IsStalled())
  {
    endpoint_.out0->ClearStall();
  }

  ErrorCode ans = ErrorCode::OK;

  switch (type)
  {
    case RequestType::STANDARD:
      ans = ProcessStandardRequest(in_isr, setup, direction, recipient);
      break;
    case RequestType::CLASS:
      ans = ProcessClassRequest(in_isr, setup, direction, recipient);
      break;
    case RequestType::VENDOR:
      ans = ProcessVendorRequest(in_isr, setup, direction, recipient);
      break;
    default:
      ans = ErrorCode::ARG_ERR;
      break;
  }

  if (ans != ErrorCode::OK)
  {
    StallControlEndpoint();
  }
}

ErrorCode DeviceCore::ProcessStandardRequest(bool in_isr, const SetupPacket *&setup,
                                             RequestDirection direction,
                                             Recipient recipient)
{
  UNUSED(in_isr);
  UNUSED(direction);
  UNUSED(recipient);
  // 根据 recipient 处理不同目标的请求
  StandardRequest req = static_cast<StandardRequest>(setup->bRequest);

  ErrorCode ans = ErrorCode::OK;

  switch (req)
  {
    case StandardRequest::GET_STATUS:
      // 设备/接口/端点的状态请求
      ans = RespondWithStatus(setup, recipient);
      break;
    case StandardRequest::CLEAR_FEATURE:
      // 解除某个特性（如端点的HALT）
      ans = ClearFeature(setup, recipient);
      break;
    case StandardRequest::SET_FEATURE:
      // 设置特性（如Remote Wakeup/端点STALL）
      ans = ApplyFeature(setup, recipient);
      break;
    case StandardRequest::SET_ADDRESS:
      // 设置USB地址，status阶段后生效
      ans = PrepareAddressChange(setup->wValue);
      break;
    case StandardRequest::GET_DESCRIPTOR:
      // 返回设备/配置/字符串等描述符
      ans = SendDescriptor(in_isr, setup, recipient);
      break;
    case StandardRequest::SET_DESCRIPTOR:
      // TODO: 很少用，可选实现
      break;
    case StandardRequest::GET_CONFIGURATION:
      // 返回当前配置
      ans = SendConfiguration();
      break;
    case StandardRequest::SET_CONFIGURATION:
      // 设置当前配置（切换Config描述符索引）
      ans = SwitchConfiguration(setup->wValue);
      break;
    case StandardRequest::GET_INTERFACE:
    {
      if (recipient != Recipient::INTERFACE)
      {
        ans = ErrorCode::ARG_ERR;
        break;
      }

      uint8_t interface_index = static_cast<uint8_t>(setup->wIndex & 0xFF);

      uint8_t alt = 0;
      auto item = config_desc_.GetItemByInterfaceNum(interface_index);

      if (item != nullptr)
      {
        item->GetAltSetting(interface_index, alt);
      }
      else
      {
        ans = ErrorCode::NOT_FOUND;
        break;
      }

      DevWriteEP0Data(LibXR::ConstRawData(&alt, 1), endpoint_.in0->MaxTransferSize(), 1);

      return ErrorCode::OK;
    }
    case StandardRequest::SET_INTERFACE:
    {
      if (recipient != Recipient::INTERFACE)
      {
        ans = ErrorCode::ARG_ERR;
        break;
      }

      uint8_t interface_index = static_cast<uint8_t>(setup->wIndex & 0xFF);
      uint8_t alt_setting = static_cast<uint8_t>(setup->wValue);

      auto item = config_desc_.GetItemByInterfaceNum(interface_index);
      if (item == nullptr)
      {
        ans = ErrorCode::NOT_FOUND;
        break;
      }

      ans = item->SetAltSetting(interface_index, alt_setting);
      if (ans == ErrorCode::OK)
      {
        WriteZLP();
      }
      break;
    }
    case StandardRequest::SYNCH_FRAME:
      // TODO: 一般仅用于同步端点
      ans = ErrorCode::NOT_SUPPORT;
      break;
    default:
      // 未知请求，直接STALL
      ans = ErrorCode::ARG_ERR;
  }

  return ans;
}

ErrorCode DeviceCore::RespondWithStatus(const SetupPacket *setup, Recipient recipient)
{
  if (setup->wLength != 2)
  {
    return ErrorCode::ARG_ERR;
  }

  uint16_t status = 0;
  switch (recipient)
  {
    case Recipient::DEVICE:
      status = config_desc_.GetDeviceStatus();
      break;
    case Recipient::INTERFACE:
      status = 0x0000;
      break;
    case Recipient::ENDPOINT:
    {
      uint8_t ep_addr = setup->wIndex & 0xFF;
      Endpoint *ep = nullptr;
      endpoint_.pool.FindEndpoint(ep_addr, ep);

      if (ep == nullptr)
      {
        return ErrorCode::NOT_FOUND;
      }

      if (ep->GetState() == Endpoint::State::STALLED)
      {
        status = 0x0001;
      }
      break;
    }
    default:
      return ErrorCode::ARG_ERR;
  }

  LibXR::ConstRawData data(&status, 2);
  DevWriteEP0Data(data, endpoint_.in0->MaxTransferSize(), setup->wLength);

  return ErrorCode::OK;
}

ErrorCode DeviceCore::ClearFeature(const SetupPacket *setup, Recipient recipient)
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
        endpoint_.pool.FindEndpoint(ep_addr, ep);
        if (ep)
        {
          ep->ClearStall();
          // 状态阶段：回复ZLP
          WriteZLP();
        }
        else
        {
          return ErrorCode::NOT_FOUND;
        }
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }
      break;
    }
    case Recipient::DEVICE:
      // 可选：处理Remote Wakeup等设备级特性
      if (setup->wValue == 1)  // 1 = DEVICE_REMOTE_WAKEUP
      {
        DisableRemoteWakeup();
        WriteZLP();
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  return ErrorCode::OK;
}

ErrorCode DeviceCore::ApplyFeature(const SetupPacket *setup, Recipient recipient)
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
        endpoint_.pool.FindEndpoint(ep_addr, ep);
        if (ep)
        {
          ep->Stall();  // 设置端点STALL
          // 状态阶段回复ZLP
          WriteZLP();
        }
        else
        {
          return ErrorCode::NOT_FOUND;
        }
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }
      break;
    }
    case Recipient::DEVICE:
      if (setup->wValue == 1)  // 1 = DEVICE_REMOTE_WAKEUP
      {
        EnableRemoteWakeup();
        WriteZLP();
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  return ErrorCode::OK;
}

ErrorCode DeviceCore::SendDescriptor(bool in_isr, const SetupPacket *setup,
                                     Recipient recipient)
{
  uint8_t desc_type = (setup->wValue >> 8) & 0xFF;
  uint8_t desc_idx = (setup->wValue) & 0xFF;
  ConstRawData data = {nullptr, 0};
  switch (desc_type)
  {
    case 0x01:  // DEVICE
      data = device_desc_.GetData();
      if (!config_desc_.IsComposite())
      {
        config_desc_.OverrideDeviceDescriptor(device_desc_);
      }
      break;
    case 0x02:  // CONFIGURATION
      config_desc_.Generate();
      data = config_desc_.GetData();
      break;
    case 0x03:  // STRING
    {
      uint8_t string_idx = desc_idx;
      uint16_t lang = setup->wIndex;
      if (string_idx == 0)
      {
        data = strings_.GetLangIDData();
      }
      else
      {
        ErrorCode ec = strings_.GenerateString(
            static_cast<DescriptorStrings::Index>(string_idx), lang);
        if (ec != ErrorCode::OK)
        {
          return ec;
        }
        data = strings_.GetData();
      }
      break;
    }
    case 0x06:  // Device Qualifier (USB 2.0+)
                // TODO: Device Qualifier HS Only
    case 0x07:  // Other Speed Configurations
      return ErrorCode::NOT_SUPPORT;
    default:
    {
      uint8_t intf_num = 0;

      if (recipient == Recipient::INTERFACE)
      {
        intf_num = setup->wIndex & 0xFF;
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }

      auto *item =
          reinterpret_cast<DeviceClass *>(config_desc_.GetItemByInterfaceNum(intf_num));
      if (item && item->OnGetDescriptor(in_isr, setup->bRequest, setup->wValue,
                                        setup->wLength, data) == ErrorCode::OK)
      {
        break;  // 成功处理
      }
      else
      {
        return ErrorCode::ARG_ERR;
      }
    }
  }

  DevWriteEP0Data(data, endpoint_.in0->MaxTransferSize(), setup->wLength);

  return ErrorCode::OK;
}

ErrorCode DeviceCore::PrepareAddressChange(uint16_t address)
{
  state_.pending_addr = static_cast<uint8_t>(address & 0x7F);

  WriteZLP(Context::STATUS_IN);
  return SetAddress(address, Context::SETUP);
}

ErrorCode DeviceCore::SwitchConfiguration(uint16_t value)
{
  if (value == 0)  // reset
  {
    // TODO: reset
    return ErrorCode::NOT_SUPPORT;
  }

  if (config_desc_.SwitchConfig(value) != ErrorCode::OK)
  {
    return ErrorCode::NOT_FOUND;
  }
  // ACK status阶段
  WriteZLP();

  return ErrorCode::OK;
}

ErrorCode DeviceCore::SendConfiguration()
{
  uint8_t cfg = config_desc_.GetCurrentConfig();
  LibXR::ConstRawData data(&cfg, 1);
  DevWriteEP0Data(data, endpoint_.in0->MaxTransferSize(), 1);

  return ErrorCode::OK;
}

void DeviceCore::StallControlEndpoint()
{
  endpoint_.pool.GetEndpoint0Out()->Stall();
  endpoint_.pool.GetEndpoint0In()->Stall();
}

void DeviceCore::ClearControlEndpointStall()
{
  endpoint_.pool.GetEndpoint0Out()->ClearStall();
  endpoint_.pool.GetEndpoint0In()->ClearStall();
}

ErrorCode DeviceCore::ProcessClassRequest(bool in_isr, const SetupPacket *setup,
                                          RequestDirection /*direction*/,
                                          Recipient recipient)
{
  // 只处理 Class 请求（bmRequestType bits[6:5] == 01）
  if ((setup->bmRequestType & 0x60) != 0x20)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  DeviceClass *item = nullptr;

  switch (recipient)
  {
    case Recipient::INTERFACE:
    {
      // 低字节 = 接口号
      uint8_t if_num = static_cast<uint8_t>(setup->wIndex & 0xFF);
      item = reinterpret_cast<DeviceClass *>(config_desc_.GetItemByInterfaceNum(if_num));
      break;
    }
    case Recipient::ENDPOINT:
    {
      // 低字节 = 端点地址（含 0x80 方向位）
      uint8_t ep_addr = static_cast<uint8_t>(setup->wIndex & 0xFF);
      item = reinterpret_cast<DeviceClass *>(config_desc_.GetItemByEndpointAddr(ep_addr));
      break;
    }
    default:
      return ErrorCode::NOT_SUPPORT;  // Device/Other 一般不在这里处理
  }

  if (!item)
  {
    return ErrorCode::NOT_FOUND;
  }

  DeviceClass::RequestResult result{};
  auto ec = item->OnClassRequest(in_isr, setup->bRequest, setup->wValue, setup->wLength,
                                 setup->wIndex, result);
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  // 不允许同时提供读写缓冲
  const bool HAS_READ_BUF = (result.read_data.size_ > 0);
  const bool HAS_WRITE_BUFF = (result.write_data.size_ > 0);
  if (HAS_READ_BUF && HAS_WRITE_BUFF)
  {
    return ErrorCode::ARG_ERR;
  }

  // Host->Device（OUT）类请求通常需要设备“读入”主机数据：read_data
  if (HAS_READ_BUF)
  {
    if (setup->wLength == 0 || result.read_data.size_ < setup->wLength)
    {
      return ErrorCode::ARG_ERR;  // 长度不匹配
    }

    class_req_.read = true;
    class_req_.class_ptr = item;
    class_req_.b_request = setup->bRequest;

    DevReadEP0Data(result.read_data, endpoint_.in0->MaxTransferSize());
    return ErrorCode::OK;
  }

  // Device->Host（IN）类请求需要设备“写出”数据给主机：write_data
  if (HAS_WRITE_BUFF)
  {
    if (setup->wLength == 0)
    {
      return ErrorCode::ARG_ERR;  // 长度不匹配
    }

    class_req_.write = true;
    class_req_.class_ptr = item;
    class_req_.b_request = setup->bRequest;

    DevWriteEP0Data(result.write_data, endpoint_.in0->MaxTransferSize());
    return ErrorCode::OK;
  }

  // 无数据阶段：按类的意愿发送/接收 ZLP（如果有）
  if (result.read_zlp)
  {
    ReadZLP();
    return ErrorCode::OK;
  }
  if (result.write_zlp)
  {
    WriteZLP();
    return ErrorCode::OK;
  }

  // 既无缓冲也无 ZLP：认为类已处理并不需要数据阶段（状态阶段由底层完成）
  return ErrorCode::OK;
}

ErrorCode DeviceCore::ProcessVendorRequest(bool in_isr, const SetupPacket *&setup,
                                           RequestDirection direction,
                                           Recipient recipient)
{
  UNUSED(in_isr);
  UNUSED(setup);
  UNUSED(direction);
  UNUSED(recipient);

  // TODO: 处理厂商自定义请求
  return ErrorCode::NOT_SUPPORT;
}

Speed DeviceCore::GetSpeed() const { return state_.speed; }
