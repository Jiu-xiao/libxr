#pragma once

namespace LibXR::USB
{
template <typename C>
DeviceCore<C>::DeviceCore(
    EndpointPool& pool, USBSpec spec, Speed speed,
    DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
    const std::initializer_list<const DescriptorStrings::LanguagePack*>& languages,
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs,
    ConstRawData uid)
    : pool_(pool),
      composition_(pool, languages, configs, uid),
      device_desc_(spec, packet_size, vid, pid, bcd, composition_.GetConfigNum()),
      speed_(speed),
      ep0_packet_size_(static_cast<uint8_t>(packet_size)),
      profile_speeds_(speed == Speed::HIGH
                          ? (SpeedBit(Speed::FULL) | SpeedBit(Speed::HIGH))
                          : SpeedBit(speed)),
      control_out_(new uint8_t[composition_.ControlReceiveCapacity()],
                   composition_.ControlReceiveCapacity())
{
  static_assert(
      (C::SPEEDS &
       ~(SpeedBit(Speed::LOW) | SpeedBit(Speed::FULL) | SpeedBit(Speed::HIGH))) == 0U,
      "A SuperSpeed policy requires its descriptor/link backend implementation");
  REQUIRE((C::SPEEDS & SpeedBit(speed)) != 0U);
  REQUIRE(ep0_packet_size_ == 8U || ep0_packet_size_ == 16U || ep0_packet_size_ == 32U ||
          ep0_packet_size_ == 64U);
  REQUIRE(speed != Speed::HIGH || ep0_packet_size_ == 64U);
  REQUIRE(speed != Speed::LOW || ep0_packet_size_ == 8U);
  if constexpr (C::BOS) this->InitializeBos(composition_);
  pool_.SetSpeed(speed);
  pool_.SetControlHandler(
      Callback<uint32_t>::Create([](bool context, DeviceCore* self, uint32_t events)
                                 { self->ProcessEvents(context, events); }, this));
}

template <typename C>
void DeviceCore<C>::Initialize(bool in_isr)
{
  in0_ = pool_.GetEndpoint0In();
  out0_ = pool_.GetEndpoint0Out();
  REQUIRE(in0_ != nullptr && out0_ != nullptr);
  in0_->Configure({Endpoint::Direction::IN, Endpoint::Type::CONTROL, ep0_packet_size_});
  out0_->Configure({Endpoint::Direction::OUT, Endpoint::Type::CONTROL, ep0_packet_size_});
  in0_->SetOnTransferCompleteCallback(in_callback_);
  out0_->SetOnTransferCompleteCallback(out_callback_);
  in0_->SetOnTransferError(error_callback_);
  out0_->SetOnTransferError(error_callback_);
  composition_.Init(in_isr);
  const ErrorCode result = composition_.PrepareDescriptors(profile_speeds_, in_isr);
  if (result != ErrorCode::OK || in0_->GetState() == Endpoint::State::ERROR ||
      out0_->GetState() == Endpoint::State::ERROR)
  {
    inited_.store(0U, std::memory_order_release);
    return;
  }
  {
    EndpointPool::HardwareScope hardware(&pool_);
    OnControlReady();
  }
  inited_.store(1U, std::memory_order_release);
}

template <typename C>
void DeviceCore<C>::Deinitialize(bool in_isr)
{
  Abort(in_isr);
  inited_.store(0U, std::memory_order_release);
  if (composition_.GetCurrentConfig())
    composition_.Notify(in_isr, DeviceEvent::DECONFIGURED);
  composition_.Deinit(in_isr);
  if (in0_) in0_->Close();
  if (out0_) out0_->Close();
}

template <typename C>
void DeviceCore<C>::ProcessEvents(bool in_isr, uint32_t events)
{
  if (events & (RESET_EVENT | DISCONNECT_EVENT | DEINIT_EVENT))
  {
    Deinitialize(in_isr);
    if ((events & RESET_EVENT) && desired_enabled_.load(std::memory_order_acquire))
    {
      {
        EndpointPool::HardwareScope hardware(&pool_);
        OnControllerReset();
      }
      pool_.SetSuspended(false, in_isr);
      const Speed observed_speed = ReadBusSpeed();
      if ((profile_speeds_ & SpeedBit(observed_speed)) == 0U) return;
      speed_ = observed_speed;
      pool_.SetSpeed(speed_);
      if constexpr (C::BUS_TIME) this->previous = 0xffffffffU;
      Initialize(in_isr);
      composition_.Notify(in_isr, DeviceEvent::RESET);
    }
    if (events & DISCONNECT_EVENT) composition_.Notify(in_isr, DeviceEvent::DISCONNECTED);
  }
  else if ((events & INIT_EVENT) && desired_enabled_.load(std::memory_order_acquire))
  {
    if (IsInited()) Deinitialize(in_isr);
    Initialize(in_isr);
  }
  if (events & 128U)
    for (size_t i = 0; i < composition_.ClassCount(); ++i)
      composition_.ClassAt(i)->OnService(in_isr);
  if (!IsInited()) return;
  if (events & POWER_EVENT)
  {
    const bool suspend = requested_suspend_.load(std::memory_order_acquire) != 0U;
    if (pool_.IsSuspended() != suspend)
    {
      pool_.SetSuspended(suspend, in_isr);
      composition_.Notify(in_isr,
                          suspend ? DeviceEvent::SUSPENDED : DeviceEvent::RESUMED);
    }
  }
  if (events & SETUP_EVENT)
  {
    SetupPacket setup;
    if (ReadCapturedSetup(setup))
      HandleSetup(in_isr, setup);
    else
      pool_.PostControl(SETUP_EVENT, in_isr);
  }
  if constexpr (C::BUS_TIME)
    if (events & TIME_EVENT) DeliverBusTime(in_isr);
}

template <typename C>
void DeviceCore<C>::OnSetupPacket(bool in_isr, const SetupPacket* setup)
{
  if (setup == nullptr) return;
  uint32_t words[2];
  std::memcpy(words, setup, sizeof(words));
  setup_sequence_.fetch_add(1U, std::memory_order_acq_rel);
  setup_words_[0].store(words[0], std::memory_order_relaxed);
  setup_words_[1].store(words[1], std::memory_order_relaxed);
  setup_sequence_.fetch_add(1U, std::memory_order_release);
  pool_.PostControl(SETUP_EVENT, in_isr);
}

template <typename C>
bool DeviceCore<C>::ReadCapturedSetup(SetupPacket& setup) const
{
  const uint32_t before = setup_sequence_.load(std::memory_order_acquire);
  if (before & 1U) return false;
  const uint32_t words[2] = {setup_words_[0].load(std::memory_order_relaxed),
                             setup_words_[1].load(std::memory_order_relaxed)};
  if (setup_sequence_.load(std::memory_order_acquire) != before) return false;
  std::memcpy(&setup, words, sizeof(words));
  return true;
}

template <typename C>
void DeviceCore<C>::HandleSetup(bool in_isr, const SetupPacket& setup)
{
  // Consume a genuinely captured old status first; do not invent its completion
  // merely because a new request arrived. Nonfinal old data must not be continued.
  replacing_setup_ = true;
  in0_->RetireCapturedCompletion(in_isr);
  out0_->RetireCapturedCompletion(in_isr);
  replacing_setup_ = false;
  Abort(in_isr);
  if (in0_->IsStalled()) (void)in0_->ClearStall();
  if (out0_->IsStalled()) (void)out0_->ClearStall();
  in0_->ResetAfterHardwareStop();
  out0_->ResetAfterHardwareStop();
  setup_ = setup;
  if constexpr ((C::SPEEDS & SpeedBit(Speed::HIGH)) != 0U)
    this->other_speed_descriptor = false;
  handled_setup_sequence_ = setup_sequence_.load(std::memory_order_acquire);
  transferred_ = 0;
  status_out_armed_ = false;
  status_out_seen_ = false;
  const auto type = static_cast<RequestType>(setup.bmRequestType & REQ_TYPE_MASK);
  ErrorCode result = type == RequestType::STANDARD ? HandleStandardRequest(in_isr)
                                                   : ClassRequest(in_isr);
  if (result != ErrorCode::OK) Stall(in_isr);
}

template <typename C>
void DeviceCore<C>::Abort(bool in_isr)
{
  DeviceClass* previous = control_class_;
  const SetupPacket old = setup_;
  control_class_ = nullptr;
  phase_ = Phase::IDLE;
  control_in_ = {nullptr, 0};
  legacy_out_ = {nullptr, 0};
  transferred_ = 0;
  in_segment_size_ = 0;
  in_tail_zero_ = false;
  status_out_armed_ = false;
  status_out_seen_ = false;
  pending_address_ = 0xff;
  if (previous) previous->OnControlAbort(in_isr, old);
}

template <typename C>
void DeviceCore<C>::Finish(bool in_isr)
{
  DeviceClass* previous = control_class_;
  const SetupPacket completed = setup_;
  control_class_ = nullptr;
  phase_ = Phase::IDLE;
  status_out_armed_ = false;
  status_out_seen_ = false;
  {
    EndpointPool::HardwareScope hardware(&pool_);
    OnControlReady();
  }
  if (previous) previous->OnControlComplete(in_isr, completed);
}

template <typename C>
void DeviceCore<C>::Stall(bool in_isr)
{
  Abort(in_isr);
  phase_ = Phase::STALLED;
  if (in0_) (void)in0_->Stall();
  if (out0_) (void)out0_->Stall();
}

template <typename C>
void DeviceCore<C>::StatusIn()
{
  phase_ = Phase::STATUS_IN;
  (void)in0_->Transfer(0U);
}

template <typename C>
void DeviceCore<C>::ArmStatusOut()
{
  if (status_out_armed_) return;
  status_out_armed_ = true;
  (void)out0_->ArmReceive(0U);
}

template <typename C>
void DeviceCore<C>::WriteControl(ConstRawData data)
{
  control_in_ = {data.addr_, LibXR::min(data.size_, static_cast<size_t>(setup_.wLength))};
  if (control_class_ && control_in_.size_ > ep0_packet_size_ &&
      control_in_.size_ <= control_out_.size_)
  {
    Memory::FastCopy(control_out_.addr_, control_in_.addr_, control_in_.size_);
    control_in_.addr_ = control_out_.addr_;
  }
  transferred_ = 0;
  in_tail_zero_ = control_in_.size_ != 0U && control_in_.size_ < setup_.wLength &&
                  control_in_.size_ % ep0_packet_size_ == 0U;
  phase_ = Phase::DATA_IN;
  SendNextIn();
}

template <typename C>
void DeviceCore<C>::SendNextIn()
{
  const size_t remaining = control_in_.size_ - transferred_;
  const size_t amount = LibXR::min(remaining, static_cast<size_t>(ep0_packet_size_));
  auto buffer = in0_->GetBuffer();
  REQUIRE(amount <= buffer.size_);
  if (amount)
    Memory::FastCopy(buffer.addr_,
                     static_cast<const uint8_t*>(control_in_.addr_) + transferred_,
                     amount);
  if constexpr ((C::SPEEDS & SpeedBit(Speed::HIGH)) != 0U)
    if (this->other_speed_descriptor && transferred_ <= 1U && transferred_ + amount > 1U)
      static_cast<uint8_t*>(buffer.addr_)[1U - transferred_] = 7U;
  in_segment_size_ = amount;
  // Early STATUS OUT arming is a hardware compatibility measure only. Completion
  // is still driven by the actual status packet, never by this arming call.
  ArmStatusOut();
  (void)in0_->Transfer(amount);
}

template <typename C>
void DeviceCore<C>::ReceiveNextOut()
{
  const size_t remaining = setup_.wLength - transferred_;
  (void)out0_->ArmReceive(LibXR::min(remaining, static_cast<size_t>(ep0_packet_size_)));
}

template <typename C>
void DeviceCore<C>::InComplete(bool in_isr, ConstRawData& data)
{
  if (!IsInited()) return;
  if (phase_ == Phase::STATUS_IN)
  {
    if (data.size_ != 0U)
    {
      Stall(in_isr);
      return;
    }
    if (pending_address_ != 0xff)
    {
      (void)SetAddress(pending_address_, Context::STATUS_IN_COMPLETE);
      pending_address_ = 0xff;
    }
    Finish(in_isr);
    return;
  }
  if (phase_ != Phase::DATA_IN || data.size_ != in_segment_size_)
  {
    if (!replacing_setup_) Stall(in_isr);
    return;
  }
  transferred_ += data.size_;
  if (transferred_ < control_in_.size_)
  {
    if (!replacing_setup_ && !status_out_seen_) SendNextIn();
    return;
  }
  if (in_tail_zero_)
  {
    if (replacing_setup_) return;
    in_tail_zero_ = false;
    in_segment_size_ = 0;
    (void)in0_->Transfer(0U);
    return;
  }
  if (control_class_)
  {
    ConstRawData complete = control_in_;
    if (control_class_->OnControlData(in_isr, setup_, complete) != ErrorCode::OK)
    {
      Stall(in_isr);
      return;
    }
  }
  phase_ = Phase::STATUS_OUT;
  if (status_out_seen_) Finish(in_isr);
}

template <typename C>
void DeviceCore<C>::OutComplete(bool in_isr, ConstRawData& data)
{
  if (!IsInited()) return;
  if (status_out_armed_ && (phase_ == Phase::DATA_IN || phase_ == Phase::STATUS_OUT))
  {
    if (data.size_ != 0U)
    {
      Stall(in_isr);
      return;
    }
    status_out_seen_ = true;
    if (phase_ == Phase::STATUS_OUT) Finish(in_isr);
    return;
  }
  if (phase_ != Phase::DATA_OUT || replacing_setup_) return;
  const size_t remaining = setup_.wLength - transferred_;
  if (data.size_ > remaining || transferred_ + data.size_ > control_out_.size_)
  {
    Stall(in_isr);
    return;
  }
  if (data.size_)
    Memory::FastCopy(static_cast<uint8_t*>(control_out_.addr_) + transferred_, data.addr_,
                     data.size_);
  transferred_ += data.size_;
  if (transferred_ < setup_.wLength && data.size_ == ep0_packet_size_)
  {
    ReceiveNextOut();
    return;
  }
  ConstRawData full{control_out_.addr_, transferred_};
  if (legacy_out_.addr_ && full.size_)
    Memory::FastCopy(legacy_out_.addr_, full.addr_, full.size_);
  if (control_class_ == nullptr ||
      control_class_->OnControlData(in_isr, setup_, full) != ErrorCode::OK)
  {
    Stall(in_isr);
    return;
  }
  StatusIn();
}

template <typename C>
ErrorCode DeviceCore<C>::ClassRequest(bool in_isr)
{
  const auto type = static_cast<RequestType>(setup_.bmRequestType & REQ_TYPE_MASK);
  if (type != RequestType::CLASS && type != RequestType::VENDOR)
    return ErrorCode::NOT_SUPPORT;
  if constexpr (C::BOS)
  {
    if (type == RequestType::VENDOR)
    {
      this->SelectBos(composition_);
      BosVendorResult bos{};
      const auto result = this->manager->ProcessVendorRequest(in_isr, &setup_, bos);
      if (result == ErrorCode::OK && bos.handled)
      {
        if (setup_.wLength && (setup_.bmRequestType & REQ_DIRECTION_MASK))
          WriteControl(bos.in_data);
        else if (setup_.wLength == 0U)
          StatusIn();
        else
          return ErrorCode::ARG_ERR;
        return ErrorCode::OK;
      }
      if (result != ErrorCode::OK && result != ErrorCode::NOT_SUPPORT) return result;
    }
  }
  const auto recipient =
      static_cast<Recipient>(setup_.bmRequestType & REQ_RECIPIENT_MASK);
  DeviceClass* item =
      recipient == Recipient::INTERFACE
          ? composition_.FindClassByInterfaceNumber(setup_.wIndex & 0xffU)
      : recipient == Recipient::ENDPOINT
          ? composition_.FindClassByEndpointAddress(static_cast<uint8_t>(setup_.wIndex))
          : nullptr;
  if (!item) return ErrorCode::NOT_FOUND;
  typename DeviceClass::ControlTransferResult result{};
  const ErrorCode answer = item->OnControlRequest(in_isr, setup_, result);
  if (answer != ErrorCode::OK) return answer;
  control_class_ = item;
  if (setup_.wLength == 0U)
  {
    StatusIn();
    return ErrorCode::OK;
  }
  if (setup_.bmRequestType & REQ_DIRECTION_MASK)
  {
    if (result.read_data.size_ != 0U ||
        (result.write_data.size_ && result.write_data.addr_ == nullptr))
      return ErrorCode::ARG_ERR;
    WriteControl(result.write_data);  // An empty IN response is a valid data stage.
    return ErrorCode::OK;
  }
  if (result.write_data.size_ || result.read_data.size_ < setup_.wLength ||
      setup_.wLength > control_out_.size_ || result.read_data.addr_ == nullptr)
    return ErrorCode::ARG_ERR;
  legacy_out_ = result.read_data;
  phase_ = Phase::DATA_OUT;
  transferred_ = 0;
  ReceiveNextOut();
  return ErrorCode::OK;
}

template <typename C>
ErrorCode DeviceCore<C>::SendDescriptor(bool in_isr)
{
  const uint8_t type = static_cast<uint8_t>(setup_.wValue >> 8U);
  const uint8_t index = static_cast<uint8_t>(setup_.wValue);
  ConstRawData data{nullptr, 0};
  switch (type)
  {
    case 1:
      if (index != 0U) return ErrorCode::ARG_ERR;
      (void)composition_.TryOverrideDeviceDescriptor(device_desc_);
      data = device_desc_.GetData();
      break;
    case 2:
      data = composition_.GetConfigDescriptor(index, speed_);
      if (data.size_ == 0U) return ErrorCode::NOT_FOUND;
      break;
    case 6:
      if constexpr ((C::SPEEDS & SpeedBit(Speed::HIGH)) != 0U)
      {
        if ((profile_speeds_ & SpeedBit(Speed::HIGH)) == 0U || index != 0U)
          return ErrorCode::NOT_SUPPORT;
        (void)composition_.TryOverrideDeviceDescriptor(device_desc_);
        this->qualifier[0] = 10U;
        this->qualifier[1] = 6U;
        const auto spec = static_cast<uint16_t>(device_desc_.GetUSBSpec());
        this->qualifier[2] = static_cast<uint8_t>(spec);
        this->qualifier[3] = static_cast<uint8_t>(spec >> 8U);
        this->qualifier[4] = static_cast<uint8_t>(device_desc_.data_.bDeviceClass);
        this->qualifier[5] = device_desc_.data_.bDeviceSubClass;
        this->qualifier[6] = device_desc_.data_.bDeviceProtocol;
        this->qualifier[7] = static_cast<uint8_t>(ep0_packet_size_);
        this->qualifier[8] = static_cast<uint8_t>(composition_.GetConfigNum());
        this->qualifier[9] = 0;
        data = {this->qualifier, sizeof(this->qualifier)};
      }
      else
        return ErrorCode::NOT_SUPPORT;
      break;
    case 7:
      if constexpr ((C::SPEEDS & SpeedBit(Speed::HIGH)) != 0U)
      {
        if ((profile_speeds_ & SpeedBit(Speed::HIGH)) == 0U)
          return ErrorCode::NOT_SUPPORT;
        const Speed other = speed_ == Speed::HIGH ? Speed::FULL : Speed::HIGH;
        data = composition_.GetConfigDescriptor(index, other);
        if (data.size_ == 0U) return ErrorCode::NOT_FOUND;
        this->other_speed_descriptor = true;
      }
      else
        return ErrorCode::NOT_SUPPORT;
      break;
    case 3:
      if (const auto result =
              composition_.GetStringDescriptor(index, setup_.wIndex, data);
          result != ErrorCode::OK)
        return result;
      break;
    case 15:
      if constexpr (C::BOS)
      {
        if (index != 0U || device_desc_.GetUSBSpec() < USBSpec::USB_2_1)
          return ErrorCode::NOT_SUPPORT;
        this->SelectBos(composition_);
        data = this->manager->GetBosDescriptor();
      }
      else
        return ErrorCode::NOT_SUPPORT;
      break;
    default:
    {
      if ((setup_.bmRequestType & REQ_RECIPIENT_MASK) !=
          static_cast<uint8_t>(Recipient::INTERFACE))
        return ErrorCode::NOT_SUPPORT;
      auto* item = composition_.FindClassByInterfaceNumber(setup_.wIndex & 0xffU);
      if (!item) return ErrorCode::NOT_FOUND;
      const auto result = item->OnGetDescriptor(in_isr, setup_.bRequest, setup_.wValue,
                                                setup_.wLength, data);
      if (result != ErrorCode::OK) return result;
    }
  }
  WriteControl(data);
  return ErrorCode::OK;
}

template <typename C>
ErrorCode DeviceCore<C>::HandleStandardRequest(bool in_isr)
{
  const bool input = (setup_.bmRequestType & REQ_DIRECTION_MASK) != 0U;
  const auto recipient =
      static_cast<Recipient>(setup_.bmRequestType & REQ_RECIPIENT_MASK);
  const auto request = static_cast<StandardRequest>(setup_.bRequest);
  switch (request)
  {
    case StandardRequest::GET_DESCRIPTOR:
      if (!input || setup_.wLength == 0U) return ErrorCode::ARG_ERR;
      return SendDescriptor(in_isr);
    case StandardRequest::SET_ADDRESS:
      if (input || recipient != Recipient::DEVICE || setup_.wIndex || setup_.wLength ||
          setup_.wValue > 127U || composition_.GetCurrentConfig())
        return ErrorCode::ARG_ERR;
      pending_address_ = static_cast<uint8_t>(setup_.wValue);
      if (const auto result = SetAddress(pending_address_, Context::SETUP_BEFORE_STATUS);
          result != ErrorCode::OK)
        return result;
      StatusIn();
      return SetAddress(pending_address_, Context::STATUS_IN_ARMED);
    case StandardRequest::GET_CONFIGURATION:
    {
      if (!input || recipient != Recipient::DEVICE || setup_.wValue || setup_.wIndex ||
          setup_.wLength != 1U)
        return ErrorCode::ARG_ERR;
      const uint8_t config = static_cast<uint8_t>(composition_.GetCurrentConfig());
      WriteControl({&config, 1});
      return ErrorCode::OK;
    }
    case StandardRequest::SET_CONFIGURATION:
    {
      if (input || recipient != Recipient::DEVICE || setup_.wIndex || setup_.wLength ||
          setup_.wValue > 255U)
        return ErrorCode::ARG_ERR;
      const auto result = composition_.SwitchConfig(setup_.wValue, in_isr);
      if (result != ErrorCode::OK) return result;
      StatusIn();
      return ErrorCode::OK;
    }
    case StandardRequest::GET_INTERFACE:
    case StandardRequest::SET_INTERFACE:
    {
      const bool get = request == StandardRequest::GET_INTERFACE;
      if (recipient != Recipient::INTERFACE || input != get || setup_.wIndex > 255U ||
          (get ? (setup_.wValue != 0U || setup_.wLength != 1U)
               : (setup_.wValue > 255U || setup_.wLength != 0U)))
        return ErrorCode::ARG_ERR;
      auto* item = composition_.FindClassByInterfaceNumber(setup_.wIndex);
      if (!item) return ErrorCode::NOT_FOUND;
      if (get)
      {
        uint8_t alt = 0;
        const auto result = item->GetAltSetting(static_cast<uint8_t>(setup_.wIndex), alt);
        if (result != ErrorCode::OK && result != ErrorCode::NOT_SUPPORT) return result;
        WriteControl({&alt, 1});
      }
      else
      {
        const auto result = item->SetAltSetting(static_cast<uint8_t>(setup_.wIndex),
                                                static_cast<uint8_t>(setup_.wValue));
        if ((result != ErrorCode::OK &&
             !(result == ErrorCode::NOT_SUPPORT && setup_.wValue == 0U)) ||
            !pool_.ConfigurationValid())
          return ErrorCode::ARG_ERR;
        StatusIn();
      }
      return ErrorCode::OK;
    }
    case StandardRequest::GET_STATUS:
    {
      if (!input || setup_.wLength != 2U || setup_.wValue) return ErrorCode::ARG_ERR;
      uint16_t status = 0;
      if (recipient == Recipient::DEVICE)
      {
        if (setup_.wIndex) return ErrorCode::ARG_ERR;
        status =
            (composition_.GetDeviceStatus() & 1U) | (IsRemoteWakeupEnabled() ? 2U : 0U);
      }
      else if (recipient == Recipient::INTERFACE)
      {
        if (setup_.wIndex > 255U ||
            !composition_.FindClassByInterfaceNumber(setup_.wIndex))
          return ErrorCode::NOT_FOUND;
      }
      else if (recipient == Recipient::ENDPOINT)
      {
        if ((setup_.wIndex & 0xff70U) != 0U) return ErrorCode::ARG_ERR;
        Endpoint* ep = nullptr;
        if (pool_.FindEndpoint(static_cast<uint8_t>(setup_.wIndex), ep) != ErrorCode::OK)
          return ErrorCode::NOT_FOUND;
        status = ep->IsStalled() ? 1U : 0U;
      }
      else
        return ErrorCode::ARG_ERR;
      const uint8_t bytes[] = {static_cast<uint8_t>(status),
                               static_cast<uint8_t>(status >> 8U)};
      WriteControl({bytes, 2});
      return ErrorCode::OK;
    }
    case StandardRequest::SET_FEATURE:
    case StandardRequest::CLEAR_FEATURE:
    {
      if (input || setup_.wLength) return ErrorCode::ARG_ERR;
      const bool set = request == StandardRequest::SET_FEATURE;
      if (recipient == Recipient::ENDPOINT)
      {
        if (setup_.wValue || (setup_.wIndex & 0xff70U) || !(setup_.wIndex & 0x0fU))
          return ErrorCode::ARG_ERR;
        Endpoint* ep = nullptr;
        if (pool_.FindEndpoint(static_cast<uint8_t>(setup_.wIndex), ep) !=
                ErrorCode::OK ||
            ep->GetType() == Endpoint::Type::ISOCHRONOUS)
          return ErrorCode::NOT_SUPPORT;
        const auto result = set ? ep->Stall() : ep->ClearStall();
        if (result != ErrorCode::OK) return result;
        composition_.Notify(
            in_isr, set ? DeviceEvent::ENDPOINT_HALTED : DeviceEvent::ENDPOINT_RESUMED,
            static_cast<uint8_t>(setup_.wIndex));
      }
      else if (recipient == Recipient::DEVICE && setup_.wValue == 1U &&
               setup_.wIndex == 0U && composition_.GetCurrentConfig() &&
               (composition_.GetDeviceStatus() & 2U))
      {
        if (set)
          EnableRemoteWakeup();
        else
          DisableRemoteWakeup();
      }
      else
        return ErrorCode::NOT_SUPPORT;
      StatusIn();
      return ErrorCode::OK;
    }
    case StandardRequest::SYNCH_FRAME:
      if constexpr (C::BUS_TIME)
      {
        if (!input || recipient != Recipient::ENDPOINT || setup_.wValue ||
            setup_.wLength != 2U || (setup_.wIndex & 0xff70U))
          return ErrorCode::ARG_ERR;
        Endpoint* ep = nullptr;
        if (pool_.FindEndpoint(static_cast<uint8_t>(setup_.wIndex), ep) !=
                ErrorCode::OK ||
            ep->GetType() != Endpoint::Type::ISOCHRONOUS)
          return ErrorCode::NOT_SUPPORT;
        const uint16_t frame = static_cast<uint16_t>(
            this->observed.load(std::memory_order_acquire) & 0x7ffU);
        const uint8_t bytes[] = {static_cast<uint8_t>(frame),
                                 static_cast<uint8_t>(frame >> 8U)};
        WriteControl({bytes, 2});
        return ErrorCode::OK;
      }
      return ErrorCode::NOT_SUPPORT;
    default:
      return ErrorCode::NOT_SUPPORT;
  }
}

template <typename C>
void DeviceCore<C>::OnSof(bool in_isr, uint16_t frame, uint8_t microframe)
{
  if constexpr (C::BUS_TIME)
  {
    this->observed_ms.store(static_cast<uint32_t>(Timebase::GetMilliseconds()),
                            std::memory_order_relaxed);
    this->observed.store((frame == 0xffffU ? 0xffffU : (frame & 0x7ffU)) |
                             (static_cast<uint32_t>(microframe) << 16U),
                         std::memory_order_release);
    if (composition_.WantsBusTime()) pool_.PostControl(TIME_EVENT, in_isr);
  }
  else
  {
    UNUSED(in_isr);
    UNUSED(frame);
    UNUSED(microframe);
  }
}

template <typename C>
void DeviceCore<C>::DeliverBusTime(bool in_isr)
{
  if constexpr (C::BUS_TIME)
  {
    const uint32_t now = this->observed.load(std::memory_order_acquire);
    const uint32_t ms = this->observed_ms.load(std::memory_order_relaxed);
    BusTime time;
    time.frame = static_cast<uint16_t>(now & 0xffffU);
    time.microframe = static_cast<uint8_t>(now >> 16U);
    if (this->previous != 0xffffffffU && time.frame != 0xffffU &&
        (this->previous & 0xffffU) != 0xffffU && ms - this->previous_ms < 2048U)
    {
      const uint8_t previous_micro = static_cast<uint8_t>(this->previous >> 16U);
      if ((previous_micro == 0xffU) == (time.microframe == 0xffU))
      {
        if (time.microframe == 0xffU)
          time.elapsed_intervals = (time.frame - (this->previous & 0x7ffU)) & 0x7ffU;
        else
        {
          const uint32_t current = time.frame * 8U + time.microframe;
          const uint32_t previous = (this->previous & 0x7ffU) * 8U + previous_micro;
          time.elapsed_intervals = (current - previous) & 0x3fffU;
        }
        time.discontinuity = time.elapsed_intervals == 0U;
      }
    }
    this->previous = now;
    this->previous_ms = ms;
    composition_.NotifyBusTime(in_isr, time);
  }
}
}  // namespace LibXR::USB
