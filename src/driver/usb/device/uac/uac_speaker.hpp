#pragma once

#include <cstddef>
#include <cstdint>

#include "core.hpp"
#include "desc_cfg.hpp"
#include "dev_core.hpp"
#include "ep.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"

namespace LibXR::USB
{

template <uint8_t CHANNELS, uint8_t BITS_PER_SAMPLE>
class UAC1SpeakerQ : public DeviceClass
{
 public:
  static_assert(CHANNELS >= 1 && CHANNELS <= 8, "CHANNELS out of range");
  static_assert(BITS_PER_SAMPLE == 8 || BITS_PER_SAMPLE == 16 || BITS_PER_SAMPLE == 24,
                "BITS_PER_SAMPLE must be 8/16/24");

  static constexpr const char* DEFAULT_CONTROL_INTERFACE_STRING = "XRUSB UAC1 Control";
  static constexpr const char* DEFAULT_STREAMING_INTERFACE_STRING =
      "XRUSB UAC1 Speaker";

 private:
  static constexpr uint8_t K_SUBFRAME_SIZE = (BITS_PER_SAMPLE <= 8)    ? 1
                                            : (BITS_PER_SAMPLE <= 16) ? 2
                                                                      : 3;
  static constexpr int16_t K_DEFAULT_VOL_MIN = 0;
  static constexpr int16_t K_DEFAULT_VOL_MAX = 0;
  static constexpr int16_t K_DEFAULT_VOL_RES = 1;
  static constexpr Speed K_DEFAULT_SPEED = Speed::FULL;
  static constexpr size_t K_DEFAULT_QUEUE_BYTES = 4096;
  static constexpr uint8_t K_DEFAULT_INTERVAL = 1;

 public:
  UAC1SpeakerQ(
      uint32_t sample_rate_hz, int16_t vol_min = K_DEFAULT_VOL_MIN,
      int16_t vol_max = K_DEFAULT_VOL_MAX, int16_t vol_res = K_DEFAULT_VOL_RES,
      Speed speed = K_DEFAULT_SPEED, size_t queue_bytes = K_DEFAULT_QUEUE_BYTES,
      uint8_t interval = K_DEFAULT_INTERVAL,
      Endpoint::EPNumber iso_out_ep_num = Endpoint::EPNumber::EP_AUTO,
      const char* control_interface_string = DEFAULT_CONTROL_INTERFACE_STRING,
      const char* streaming_interface_string = DEFAULT_STREAMING_INTERFACE_STRING)
      : iso_out_ep_num_(iso_out_ep_num),
        control_interface_string_(control_interface_string),
        streaming_interface_string_(streaming_interface_string),
        vol_min_(vol_min),
        vol_max_(vol_max),
        vol_res_(vol_res),
        interval_(interval),
        speed_(speed),
        sr_hz_(sample_rate_hz),
        pcm_queue_(queue_bytes)
  {
    RecomputeTiming();
    sf_cur_[0] = static_cast<uint8_t>(sr_hz_ & 0xFF);
    sf_cur_[1] = static_cast<uint8_t>((sr_hz_ >> 8) & 0xFF);
    sf_cur_[2] = static_cast<uint8_t>((sr_hz_ >> 16) & 0xFF);
  }

  const char* GetInterfaceString(size_t local_interface_index) const override
  {
    switch (local_interface_index)
    {
      case 0:
        return control_interface_string_;
      case 1:
        return streaming_interface_string_;
      default:
        return nullptr;
    }
  }

  ErrorCode ReadPcm(void* data, size_t nbytes)
  {
    return pcm_queue_.PopBatch(reinterpret_cast<uint8_t*>(data), nbytes);
  }

  size_t QueueSize() const { return pcm_queue_.Size(); }
  size_t QueueSpace() { return pcm_queue_.EmptySize(); }
  void ResetQueue()
  {
    pcm_queue_.Reset();
    rx_dropped_bytes_ = 0;
  }
  size_t DroppedBytes() const { return rx_dropped_bytes_; }

  enum : uint8_t
  {
    USB_CLASS_AUDIO = 0x01,
    SUBCLASS_AC = 0x01,
    SUBCLASS_AS = 0x02,
    CS_INTERFACE = 0x24,
    CS_ENDPOINT = 0x25,
    AC_HEADER = 0x01,
    AC_INPUT_TERMINAL = 0x02,
    AC_OUTPUT_TERMINAL = 0x03,
    AC_FEATURE_UNIT = 0x06,
    AS_GENERAL = 0x01,
    AS_FORMAT_TYPE = 0x02,
    EP_GENERAL = 0x01,
    FORMAT_TYPE_I = 0x01,
    SET_CUR = 0x01,
    GET_CUR = 0x81,
    SET_MIN = 0x02,
    GET_MIN = 0x82,
    SET_MAX = 0x03,
    GET_MAX = 0x83,
    SET_RES = 0x04,
    GET_RES = 0x84,
    FU_MUTE = 0x01,
    FU_VOLUME = 0x02,
    ID_IT_USB = 1,
    ID_FU = 2,
    ID_OT_SPEAKER = 3
  };

  enum : uint16_t
  {
    WFORMAT_PCM = 0x0001
  };

#pragma pack(push, 1)
  struct CSACHeader
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AC_HEADER;
    uint16_t bcdADC = 0x0100;
    uint16_t wTotalLength = 0;
    uint8_t bInCollection = 1;
    uint8_t baInterfaceNr = 0;
  };

  struct ACInputTerminal
  {
    uint8_t bLength = 12;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AC_INPUT_TERMINAL;
    uint8_t bTerminalID = ID_IT_USB;
    uint16_t wTerminalType = 0x0101;
    uint8_t bAssocTerminal = 0;
    uint8_t bNrChannels = CHANNELS;
    uint16_t wChannelConfig = 0x0000;
    uint8_t iChannelNames = 0;
    uint8_t iTerminal = 0;
  };

  struct ACFeatureUnit
  {
    uint8_t bLength = static_cast<uint8_t>(7 + (CHANNELS + 1));
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AC_FEATURE_UNIT;
    uint8_t bUnitID = ID_FU;
    uint8_t bSourceID = ID_IT_USB;
    uint8_t bControlSize = 1;
    uint8_t bmaControlsMaster = 0x03;
    uint8_t bmaControlsCh[CHANNELS] = {0x03};
    uint8_t iFeature = 0;
  };

  struct ACOutputTerminal
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AC_OUTPUT_TERMINAL;
    uint8_t bTerminalID = ID_OT_SPEAKER;
    uint16_t wTerminalType = 0x0301;
    uint8_t bAssocTerminal = 0;
    uint8_t bSourceID = ID_FU;
    uint8_t iTerminal = 0;
  };

  struct ASGeneral
  {
    uint8_t bLength = 7;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AS_GENERAL;
    uint8_t bTerminalLink = ID_IT_USB;
    uint8_t bDelay = 1;
    uint16_t wFormatTag = WFORMAT_PCM;
  };

  struct TypeIFormat1
  {
    uint8_t bLength = 11;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = AS_FORMAT_TYPE;
    uint8_t bFormatType = FORMAT_TYPE_I;
    uint8_t bNrChannels = CHANNELS;
    uint8_t bSubframeSize = K_SUBFRAME_SIZE;
    uint8_t bBitResolution = BITS_PER_SAMPLE;
    uint8_t bSamFreqType = 1;
    uint8_t tSamFreq[3] = {0, 0, 0};
  };

  struct EndpointDescriptorIso9
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = static_cast<uint8_t>(DescriptorType::ENDPOINT);
    uint8_t bEndpointAddress = 0;
    uint8_t bmAttributes = 0x09;
    uint16_t wMaxPacketSize = 0;
    uint8_t bInterval = 0x01;
    uint8_t bRefresh = 0x00;
    uint8_t bSynchAddress = 0x00;
  };

  struct CSEndpointGeneral
  {
    uint8_t bLength = 7;
    uint8_t bDescriptorType = CS_ENDPOINT;
    uint8_t bDescriptorSubtype = EP_GENERAL;
    uint8_t bmAttributes = 0x00;
    uint8_t bLockDelayUnits = 0;
    uint16_t wLockDelay = 0;
  };

  struct UAC1DescBlock
  {
    IADDescriptor iad;
    InterfaceDescriptor ac_intf;
    CSACHeader ac_hdr;
    ACInputTerminal it_usb;
    ACFeatureUnit fu;
    ACOutputTerminal ot_speaker;
    InterfaceDescriptor as_alt0;
    InterfaceDescriptor as_alt1;
    ASGeneral as_gen;
    TypeIFormat1 fmt;
    EndpointDescriptorIso9 ep_out;
    CSEndpointGeneral ep_cs;
  };
#pragma pack(pop)

  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num, bool) override
  {
    inited_ = false;
    streaming_ = false;

    ASSERT((speed_ == Speed::HIGH && w_max_packet_size_ <= 1024) ||
           (speed_ != Speed::HIGH && interval_ == 1 && w_max_packet_size_ <= 1023));

    auto ans = endpoint_pool.Get(ep_iso_out_, Endpoint::Direction::OUT, iso_out_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ep_iso_out_->Configure({Endpoint::Direction::OUT, Endpoint::Type::ISOCHRONOUS,
                            static_cast<uint16_t>(w_max_packet_size_), true});

    itf_ac_num_ = start_itf_num;
    itf_as_num_ = static_cast<uint8_t>(start_itf_num + 1);

    desc_block_.iad = {8, static_cast<uint8_t>(DescriptorType::IAD), itf_ac_num_, 2,
                       USB_CLASS_AUDIO, SUBCLASS_AC, 0x00, 0};
    desc_block_.ac_intf = {9, static_cast<uint8_t>(DescriptorType::INTERFACE),
                           itf_ac_num_, 0, 0, USB_CLASS_AUDIO, SUBCLASS_AC, 0x00,
                           GetInterfaceStringIndex(0u)};
    desc_block_.ac_hdr.baInterfaceNr = itf_as_num_;
    desc_block_.ac_hdr.wTotalLength =
        static_cast<uint16_t>(sizeof(CSACHeader) + sizeof(ACInputTerminal) +
                              sizeof(ACFeatureUnit) + sizeof(ACOutputTerminal));

    if constexpr (CHANNELS == 2)
    {
      desc_block_.it_usb.wChannelConfig = 0x0003;
    }
    else
    {
      desc_block_.it_usb.wChannelConfig = 0x0000;
    }
    for (uint8_t i = 0; i < CHANNELS; ++i)
    {
      desc_block_.fu.bmaControlsCh[i] = 0x03;
    }

    desc_block_.as_alt0 = {9, static_cast<uint8_t>(DescriptorType::INTERFACE),
                           itf_as_num_, 0, 0, USB_CLASS_AUDIO, SUBCLASS_AS, 0x00,
                           GetInterfaceStringIndex(1u)};
    desc_block_.as_alt1 = {9, static_cast<uint8_t>(DescriptorType::INTERFACE),
                           itf_as_num_, 1, 1, USB_CLASS_AUDIO, SUBCLASS_AS, 0x00,
                           GetInterfaceStringIndex(1u)};

    desc_block_.as_gen.bTerminalLink = ID_IT_USB;
    desc_block_.fmt.bNrChannels = CHANNELS;
    desc_block_.fmt.bSubframeSize = K_SUBFRAME_SIZE;
    desc_block_.fmt.bBitResolution = BITS_PER_SAMPLE;
    desc_block_.fmt.tSamFreq[0] = static_cast<uint8_t>(sr_hz_ & 0xFF);
    desc_block_.fmt.tSamFreq[1] = static_cast<uint8_t>((sr_hz_ >> 8) & 0xFF);
    desc_block_.fmt.tSamFreq[2] = static_cast<uint8_t>((sr_hz_ >> 16) & 0xFF);

    desc_block_.ep_out.bEndpointAddress = static_cast<uint8_t>(
        Endpoint::EPNumberToAddr(ep_iso_out_->GetNumber(), Endpoint::Direction::OUT));
    desc_block_.ep_out.wMaxPacketSize = w_max_packet_size_;
    desc_block_.ep_out.bInterval = (speed_ == Speed::HIGH) ? interval_ : 0x01;

    ep_iso_out_->SetOnTransferCompleteCallback(on_out_complete_cb_);
    SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});
    inited_ = true;
  }

  void UnbindEndpoints(EndpointPool& endpoint_pool, bool) override
  {
    streaming_ = false;
    inited_ = false;
    if (ep_iso_out_ != nullptr)
    {
      ep_iso_out_->Close();
      ep_iso_out_->SetActiveLength(0);
      endpoint_pool.Release(ep_iso_out_);
      ep_iso_out_ = nullptr;
    }
  }

  ErrorCode OnClassRequest(bool, uint8_t bRequest, uint16_t wValue, uint16_t wLength,
                           uint16_t wIndex,
                           DeviceClass::ControlTransferResult& r) override
  {
    const uint8_t ep_addr = static_cast<uint8_t>(wIndex & 0xFF);
    const uint8_t cs_ep = static_cast<uint8_t>((wValue >> 8) & 0xFF);

    if ((wIndex & 0xFF00) == 0 && ep_addr == desc_block_.ep_out.bEndpointAddress &&
        cs_ep == 0x01)
    {
      switch (bRequest)
      {
        case GET_CUR:
        case GET_MIN:
        case GET_MAX:
          if (wLength == 3)
          {
            r.write_data = ConstRawData{sf_cur_, 3};
            return ErrorCode::OK;
          }
          break;
        case SET_CUR:
          if (wLength == 3)
          {
            pending_set_sf_ = true;
            r.read_data = RawData{sf_cur_, 3};
            return ErrorCode::OK;
          }
          break;
        case GET_RES:
          if (wLength == 3)
          {
            static uint8_t one_hz[3] = {1, 0, 0};
            r.write_data = ConstRawData{one_hz, 3};
            return ErrorCode::OK;
          }
          break;
        default:
          break;
      }
      return ErrorCode::ARG_ERR;
    }

    const uint8_t cs = static_cast<uint8_t>((wValue >> 8) & 0xFF);
    const uint8_t ch = static_cast<uint8_t>(wValue & 0xFF);
    const uint8_t ent = static_cast<uint8_t>((wIndex >> 8) & 0xFF);
    const uint8_t itf = static_cast<uint8_t>(wIndex & 0xFF);

    if (itf != itf_ac_num_ || ent != ID_FU)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    if (ch > CHANNELS)
    {
      return ErrorCode::ARG_ERR;
    }

    switch (cs)
    {
      case FU_MUTE:
        if (bRequest == SET_CUR && wLength == 1)
        {
          r.read_data = RawData{&mute_, 1};
          return ErrorCode::OK;
        }
        if (bRequest == GET_CUR && wLength == 1)
        {
          r.write_data = ConstRawData{&mute_, 1};
          return ErrorCode::OK;
        }
        return ErrorCode::ARG_ERR;

      case FU_VOLUME:
        if (wLength != 2)
        {
          return ErrorCode::ARG_ERR;
        }
        switch (bRequest)
        {
          case SET_CUR:
            r.read_data = RawData{reinterpret_cast<uint8_t*>(&vol_cur_), 2};
            return ErrorCode::OK;
          case GET_CUR:
            r.write_data = ConstRawData{reinterpret_cast<uint8_t*>(&vol_cur_), 2};
            return ErrorCode::OK;
          case GET_MIN:
            r.write_data = ConstRawData{reinterpret_cast<const uint8_t*>(&vol_min_), 2};
            return ErrorCode::OK;
          case GET_MAX:
            r.write_data = ConstRawData{reinterpret_cast<const uint8_t*>(&vol_max_), 2};
            return ErrorCode::OK;
          case GET_RES:
          case SET_RES:
            if (bRequest == SET_RES)
            {
              r.read_data = RawData{reinterpret_cast<uint8_t*>(&vol_res_), 2};
            }
            else
            {
              r.write_data =
                  ConstRawData{reinterpret_cast<const uint8_t*>(&vol_res_), 2};
            }
            return ErrorCode::OK;
          default:
            return ErrorCode::NOT_SUPPORT;
        }

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode OnClassData(bool, uint8_t bRequest, LibXR::ConstRawData&) override
  {
    if (bRequest == SET_CUR && pending_set_sf_)
    {
      const uint32_t new_sr = static_cast<uint32_t>(sf_cur_[0]) |
                              (static_cast<uint32_t>(sf_cur_[1]) << 8) |
                              (static_cast<uint32_t>(sf_cur_[2]) << 16);
      if (new_sr > 0 && new_sr != sr_hz_)
      {
        sr_hz_ = new_sr;
        RecomputeTiming();
      }
      pending_set_sf_ = false;
    }
    return ErrorCode::OK;
  }

  ErrorCode GetAltSetting(uint8_t itf, uint8_t& alt) override
  {
    if (itf != itf_as_num_)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    alt = streaming_ ? 1 : 0;
    return ErrorCode::OK;
  }

  ErrorCode SetAltSetting(uint8_t itf, uint8_t alt) override
  {
    if (itf != itf_as_num_ || ep_iso_out_ == nullptr)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    switch (alt)
    {
      case 0:
        streaming_ = false;
        ep_iso_out_->SetActiveLength(0);
        ep_iso_out_->Close();
        return ErrorCode::OK;
      case 1:
        ep_iso_out_->Configure({Endpoint::Direction::OUT, Endpoint::Type::ISOCHRONOUS,
                                static_cast<uint16_t>(w_max_packet_size_), true});
        ep_iso_out_->SetActiveLength(0);
        streaming_ = true;
        ArmOut();
        return ErrorCode::OK;
      default:
        return ErrorCode::ARG_ERR;
    }
  }

  size_t GetInterfaceCount() override { return 2; }
  bool HasIAD() override { return true; }
  size_t GetMaxConfigSize() override { return sizeof(UAC1DescBlock); }

  bool OwnsEndpoint(uint8_t ep_addr) const override
  {
    return inited_ && ep_iso_out_ != nullptr && ep_iso_out_->GetAddress() == ep_addr;
  }

 private:
  static void OnOutCompleteStatic(bool in_isr, UAC1SpeakerQ* self, ConstRawData& data)
  {
    if (self->inited_)
    {
      self->OnOutComplete(in_isr, data);
    }
  }

  void OnOutComplete(bool, ConstRawData& data)
  {
    if (streaming_ && data.size_ > 0)
    {
      if (pcm_queue_.PushBatch(reinterpret_cast<const uint8_t*>(data.addr_), data.size_) !=
          ErrorCode::OK)
      {
        rx_dropped_bytes_ += data.size_;
      }
    }
    ArmOut();
  }

  void ArmOut()
  {
    if (!streaming_ || ep_iso_out_ == nullptr ||
        ep_iso_out_->GetState() != Endpoint::State::IDLE)
    {
      return;
    }
    ep_iso_out_->Transfer(w_max_packet_size_);
  }

  void RecomputeTiming()
  {
    if (speed_ == Speed::HIGH)
    {
      uint8_t eff = interval_ ? interval_ : 1;
      if (eff > 16)
      {
        eff = 16;
      }
      service_hz_ = 8000u / (1u << (eff - 1u));
    }
    else
    {
      service_hz_ = 1000u;
    }

    const uint32_t bytes_per_sec =
        static_cast<uint32_t>(sr_hz_) * CHANNELS * K_SUBFRAME_SIZE;
    uint32_t ceil_bpt = bytes_per_sec / service_hz_;
    if ((bytes_per_sec % service_hz_) != 0)
    {
      ++ceil_bpt;
    }
    const uint32_t per_tx_limit = (speed_ == Speed::HIGH) ? 1024u : 1023u;
    if (ceil_bpt > per_tx_limit)
    {
      ceil_bpt = per_tx_limit;
    }
    w_max_packet_size_ = static_cast<uint16_t>(ceil_bpt);
    if (desc_block_.ep_out.wMaxPacketSize != 0 &&
        w_max_packet_size_ > desc_block_.ep_out.wMaxPacketSize)
    {
      w_max_packet_size_ = desc_block_.ep_out.wMaxPacketSize;
    }
  }

  Endpoint::EPNumber iso_out_ep_num_;
  Endpoint* ep_iso_out_ = nullptr;
  uint8_t itf_ac_num_ = 0;
  uint8_t itf_as_num_ = 0;
  const char* control_interface_string_ = nullptr;
  const char* streaming_interface_string_ = nullptr;

  bool inited_ = false;
  bool streaming_ = false;
  uint8_t mute_ = 0;
  int16_t vol_cur_ = 0;
  int16_t vol_min_, vol_max_, vol_res_;

  uint8_t interval_;
  Speed speed_;
  uint32_t sr_hz_;
  uint32_t service_hz_ = 1000;
  uint16_t w_max_packet_size_ = 0;
  UAC1DescBlock desc_block_;
  uint8_t sf_cur_[3] = {0, 0, 0};
  bool pending_set_sf_ = false;

  LibXR::LockFreeQueue<uint8_t> pcm_queue_;
  size_t rx_dropped_bytes_ = 0;

  LibXR::Callback<LibXR::ConstRawData&> on_out_complete_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnOutCompleteStatic, this);
};

}  // namespace LibXR::USB
