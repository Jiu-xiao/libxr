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

template <size_t MAX_FORMATS, size_t MAX_FRAMES, size_t MAX_INTERVALS_PER_FRAME>
class UVCCamera : public DeviceClass
{
 public:
  static_assert(MAX_FORMATS > 0, "MAX_FORMATS must be positive");
  static_assert(MAX_FRAMES > 0, "MAX_FRAMES must be positive");
  static_assert(MAX_INTERVALS_PER_FRAME > 0, "MAX_INTERVALS_PER_FRAME must be positive");
  static_assert(MAX_FORMATS <= UINT8_MAX, "UVC format indices are 8-bit");
  static_assert(MAX_FRAMES <= UINT8_MAX, "UVC frame indices are 8-bit");
  static_assert(MAX_INTERVALS_PER_FRAME <= UINT8_MAX,
                "UVC frame interval counts are 8-bit");

  static constexpr const char* DEFAULT_CONTROL_INTERFACE_STRING = "XRUSB UVC Control";
  static constexpr const char* DEFAULT_STREAMING_INTERFACE_STRING = "XRUSB UVC Streaming";

  struct FrameSpec
  {
    uint16_t width = 0;
    uint16_t height = 0;
    const uint32_t* frame_intervals_100ns = nullptr;
    uint8_t frame_interval_count = 0;
    uint32_t default_frame_interval_100ns = 0;
    uint32_t max_frame_buffer_size = 0;
  };

  struct FormatSpec
  {
    const uint8_t* guid_format = nullptr;
    uint8_t bits_per_pixel = 0;
    const FrameSpec* frames = nullptr;
    uint8_t frame_count = 0;
    uint8_t default_frame_index = 1;
  };

  struct Guid
  {
    uint8_t bytes[16];
  };

  static constexpr Guid FourCcGuid(char a, char b, char c, char d)
  {
    return Guid{{static_cast<uint8_t>(a),
                 static_cast<uint8_t>(b),
                 static_cast<uint8_t>(c),
                 static_cast<uint8_t>(d),
                 0x00,
                 0x00,
                 0x10,
                 0x00,
                 0x80,
                 0x00,
                 0x00,
                 0xAA,
                 0x00,
                 0x38,
                 0x9B,
                 0x71}};
  }

  UVCCamera(const FormatSpec* formats, size_t format_count, Speed speed = Speed::FULL,
            uint16_t iso_in_max_packet_size = 1023, uint8_t interval = 1,
            Endpoint::EPNumber iso_in_ep_num = Endpoint::EPNumber::EP_AUTO,
            const char* control_interface_string = DEFAULT_CONTROL_INTERFACE_STRING,
            const char* streaming_interface_string = DEFAULT_STREAMING_INTERFACE_STRING)
      : speed_(speed),
        interval_(interval),
        iso_in_max_packet_size_(iso_in_max_packet_size),
        iso_in_ep_num_(iso_in_ep_num),
        control_interface_string_(control_interface_string),
        streaming_interface_string_(streaming_interface_string)
  {
    ASSERT(RegisterFormats(formats, format_count) == ErrorCode::OK);
    SelectDefaultMode();
  }

  UVCCamera(const UVCCamera&) = delete;
  UVCCamera& operator=(const UVCCamera&) = delete;

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

  /**
   * @brief Queue one complete frame for isochronous transmission.
   *
   * The frame buffer is referenced in-place and must remain valid until
   * IsFrameBusy() returns false.
   */
  ErrorCode SubmitFrame(ConstRawData frame)
  {
    if (!streaming_)
    {
      return ErrorCode::STATE_ERR;
    }
    if (frame.addr_ == nullptr || frame.size_ == 0)
    {
      return ErrorCode::ARG_ERR;
    }
    if (frame_active_)
    {
      return ErrorCode::BUSY;
    }

    active_frame_ = frame;
    active_frame_offset_ = 0;
    frame_active_ = true;
    KickPacket();
    return ErrorCode::OK;
  }

  bool IsStreaming() const { return streaming_; }
  bool IsFrameBusy() const { return frame_active_; }
  uint8_t CurrentFormatIndex() const { return committed_.bFormatIndex; }
  uint8_t CurrentFrameIndex() const { return committed_.bFrameIndex; }
  uint32_t CurrentFrameInterval100ns() const { return committed_.dwFrameInterval; }

 private:
  enum : uint8_t
  {
    USB_CLASS_VIDEO = 0x0E,
    SUBCLASS_VC = 0x01,
    SUBCLASS_VS = 0x02,
    CS_INTERFACE = 0x24,
    VC_HEADER = 0x01,
    VC_INPUT_TERMINAL = 0x02,
    VC_OUTPUT_TERMINAL = 0x03,
    VS_INPUT_HEADER = 0x01,
    VS_FORMAT_UNCOMPRESSED = 0x04,
    VS_FRAME_UNCOMPRESSED = 0x05,
    VS_COLORFORMAT = 0x0D,
    SET_CUR = 0x01,
    GET_CUR = 0x81,
    GET_MIN = 0x82,
    GET_MAX = 0x83,
    GET_RES = 0x84,
    GET_LEN = 0x85,
    GET_INFO = 0x86,
    GET_DEF = 0x87,
    VS_PROBE_CONTROL = 0x01,
    VS_COMMIT_CONTROL = 0x02,
    PAYLOAD_HEADER_FID = 0x01,
    PAYLOAD_HEADER_EOF = 0x02,
    PAYLOAD_HEADER_EOH = 0x80,
    ID_CAMERA_IT = 1,
    ID_USB_OT = 2
  };

#pragma pack(push, 1)
  struct VCHeader
  {
    uint8_t bLength = 13;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = VC_HEADER;
    uint16_t bcdUVC = 0x0100;
    uint16_t wTotalLength = 0;
    uint32_t dwClockFrequency = 48000000;
    uint8_t bInCollection = 1;
    uint8_t baInterfaceNr = 0;
  };

  struct CameraInputTerminal
  {
    uint8_t bLength = 18;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = VC_INPUT_TERMINAL;
    uint8_t bTerminalID = ID_CAMERA_IT;
    uint16_t wTerminalType = 0x0201;
    uint8_t bAssocTerminal = 0;
    uint8_t iTerminal = 0;
    uint16_t wObjectiveFocalLengthMin = 0;
    uint16_t wObjectiveFocalLengthMax = 0;
    uint16_t wOcularFocalLength = 0;
    uint8_t bControlSize = 3;
    uint8_t bmControls[3] = {0, 0, 0};
  };

  struct OutputTerminal
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = VC_OUTPUT_TERMINAL;
    uint8_t bTerminalID = ID_USB_OT;
    uint16_t wTerminalType = 0x0101;
    uint8_t bAssocTerminal = 0;
    uint8_t bSourceID = ID_CAMERA_IT;
    uint8_t iTerminal = 0;
  };

  struct VSFormatUncompressed
  {
    uint8_t bLength = 27;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = VS_FORMAT_UNCOMPRESSED;
    uint8_t bFormatIndex = 0;
    uint8_t bNumFrameDescriptors = 0;
    uint8_t guidFormat[16] = {};
    uint8_t bBitsPerPixel = 0;
    uint8_t bDefaultFrameIndex = 1;
    uint8_t bAspectRatioX = 0;
    uint8_t bAspectRatioY = 0;
    uint8_t bmInterlaceFlags = 0;
    uint8_t bCopyProtect = 0;
  };

  struct VSColorMatching
  {
    uint8_t bLength = 6;
    uint8_t bDescriptorType = CS_INTERFACE;
    uint8_t bDescriptorSubtype = VS_COLORFORMAT;
    uint8_t bColorPrimaries = 1;
    uint8_t bTransferCharacteristics = 1;
    uint8_t bMatrixCoefficients = 4;
  };

  struct ProbeCommitControl
  {
    uint16_t bmHint = 0;
    uint8_t bFormatIndex = 1;
    uint8_t bFrameIndex = 1;
    uint32_t dwFrameInterval = 0;
    uint16_t wKeyFrameRate = 0;
    uint16_t wPFrameRate = 0;
    uint16_t wCompQuality = 0;
    uint16_t wCompWindowSize = 0;
    uint16_t wDelay = 0;
    uint32_t dwMaxVideoFrameSize = 0;
    uint32_t dwMaxPayloadTransferSize = 0;
  };
#pragma pack(pop)

  static_assert(sizeof(ProbeCommitControl) == 26, "UVC 1.0 probe/commit must be 26B");

  struct FormatRuntime
  {
    uint8_t guid_format[16] = {};
    uint8_t bits_per_pixel = 0;
    uint8_t first_frame = 0;
    uint8_t frame_count = 0;
    uint8_t default_frame_index = 1;
  };

  struct FrameRuntime
  {
    uint8_t format_index = 1;
    uint8_t frame_index = 1;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t interval_count = 0;
    uint32_t intervals[MAX_INTERVALS_PER_FRAME] = {};
    uint32_t default_interval = 0;
    uint32_t max_frame_buffer_size = 0;
  };

 public:
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num, bool) override
  {
    inited_ = false;
    streaming_ = false;
    frame_active_ = false;
    frame_id_toggle_pending_ = false;

    ASSERT(format_count_ > 0 && frame_count_ > 0);
    ASSERT((speed_ == Speed::HIGH && iso_in_max_packet_size_ <= 1024) ||
           (speed_ != Speed::HIGH && interval_ == 1 && iso_in_max_packet_size_ <= 1023));

    auto ans = endpoint_pool.Get(ep_iso_in_, Endpoint::Direction::IN, iso_in_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ep_iso_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::ISOCHRONOUS,
                           iso_in_max_packet_size_, true});

    itf_vc_num_ = start_itf_num;
    itf_vs_num_ = static_cast<uint8_t>(start_itf_num + 1);
    ep_iso_in_addr_ = Endpoint::EPNumberToAddr(ep_iso_in_->GetNumber(),
                                               Endpoint::Direction::IN);

    const size_t used = BuildDescriptors();
    SetData(RawData{descriptor_buffer_, used});

    ep_iso_in_->SetOnTransferCompleteCallback(on_in_complete_cb_);
    inited_ = true;
  }

  void UnbindEndpoints(EndpointPool& endpoint_pool, bool) override
  {
    inited_ = false;
    streaming_ = false;
    frame_active_ = false;
    frame_id_toggle_pending_ = false;
    if (ep_iso_in_ != nullptr)
    {
      ep_iso_in_->Close();
      ep_iso_in_->SetActiveLength(0);
      endpoint_pool.Release(ep_iso_in_);
      ep_iso_in_ = nullptr;
    }
  }

  ErrorCode OnClassRequest(bool, uint8_t bRequest, uint16_t wValue, uint16_t wLength,
                           uint16_t wIndex,
                           DeviceClass::ControlTransferResult& r) override
  {
    const uint8_t itf = static_cast<uint8_t>(wIndex & 0xFF);
    const uint8_t selector = static_cast<uint8_t>((wValue >> 8) & 0xFF);

    if (itf != itf_vs_num_)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    if (selector != VS_PROBE_CONTROL && selector != VS_COMMIT_CONTROL)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    switch (bRequest)
    {
      case SET_CUR:
        if (wLength != sizeof(ProbeCommitControl))
        {
          return ErrorCode::ARG_ERR;
        }
        if (selector == VS_COMMIT_CONTROL && streaming_)
        {
          return ErrorCode::STATE_ERR;
        }
        setup_selector_ = selector;
        r.read_data =
            RawData{reinterpret_cast<uint8_t*>(&setup_control_), sizeof(setup_control_)};
        return ErrorCode::OK;

      case GET_CUR:
        control_response_ = (selector == VS_COMMIT_CONTROL) ? committed_ : pending_;
        r.write_data = ConstRawData{reinterpret_cast<uint8_t*>(&control_response_),
                                    sizeof(control_response_)};
        return ErrorCode::OK;

      case GET_DEF:
      case GET_MIN:
        control_response_ = BuildControl(1, 1, frames_[formats_[0].first_frame].default_interval);
        r.write_data = ConstRawData{reinterpret_cast<uint8_t*>(&control_response_),
                                    sizeof(control_response_)};
        return ErrorCode::OK;

      case GET_MAX:
      {
        const FrameRuntime& frame = frames_[frame_count_ - 1];
        control_response_ =
            BuildControl(format_count_, frame.frame_index, frame.default_interval);
        r.write_data = ConstRawData{reinterpret_cast<uint8_t*>(&control_response_),
                                    sizeof(control_response_)};
        return ErrorCode::OK;
      }

      case GET_RES:
        control_response_ = {};
        r.write_data = ConstRawData{reinterpret_cast<uint8_t*>(&control_response_),
                                    sizeof(control_response_)};
        return ErrorCode::OK;

      case GET_LEN:
        len_response_[0] = static_cast<uint8_t>(sizeof(ProbeCommitControl));
        len_response_[1] = 0;
        r.write_data = ConstRawData{len_response_, 2};
        return ErrorCode::OK;

      case GET_INFO:
        info_response_ = 0x03;
        r.write_data = ConstRawData{&info_response_, 1};
        return ErrorCode::OK;

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode OnClassData(bool, uint8_t bRequest, LibXR::ConstRawData&) override
  {
    if (bRequest != SET_CUR)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    ProbeCommitControl normalized{};
    if (!NormalizeControl(setup_control_, normalized))
    {
      return ErrorCode::ARG_ERR;
    }

    if (setup_selector_ == VS_PROBE_CONTROL)
    {
      pending_ = normalized;
    }
    else if (setup_selector_ == VS_COMMIT_CONTROL)
    {
      committed_ = normalized;
      pending_ = normalized;
    }
    setup_selector_ = 0;
    return ErrorCode::OK;
  }

  ErrorCode GetAltSetting(uint8_t itf, uint8_t& alt) override
  {
    if (itf != itf_vs_num_)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    alt = streaming_ ? 1 : 0;
    return ErrorCode::OK;
  }

  ErrorCode SetAltSetting(uint8_t itf, uint8_t alt) override
  {
    if (itf != itf_vs_num_ || ep_iso_in_ == nullptr)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    switch (alt)
    {
      case 0:
        streaming_ = false;
        frame_active_ = false;
        frame_id_toggle_pending_ = false;
        pending_payload_ = 0;
        pending_eof_ = false;
        ep_iso_in_->SetActiveLength(0);
        return ErrorCode::OK;
      case 1:
        ep_iso_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::ISOCHRONOUS,
                               iso_in_max_packet_size_, false});
        ep_iso_in_->SetActiveLength(0);
        active_frame_offset_ = 0;
        frame_active_ = false;
        frame_id_toggle_pending_ = true;
        pending_payload_ = 0;
        pending_eof_ = false;
        streaming_ = true;
        KickPacket();
        return ErrorCode::OK;
      default:
        return ErrorCode::ARG_ERR;
    }
  }

  size_t GetInterfaceCount() override { return 2; }
  bool HasIAD() override { return true; }
  size_t GetMaxConfigSize() override { return K_MAX_CONFIG_SIZE; }

  bool OwnsEndpoint(uint8_t ep_addr) const override
  {
    return inited_ && ep_iso_in_ != nullptr && ep_iso_in_->GetAddress() == ep_addr;
  }

 private:
  ErrorCode RegisterFormats(const FormatSpec* formats, size_t format_count)
  {
    if (formats == nullptr || format_count == 0 || format_count > MAX_FORMATS)
    {
      return ErrorCode::ARG_ERR;
    }

    for (size_t i = 0; i < format_count; ++i)
    {
      const FormatSpec& src_format = formats[i];
      if (src_format.guid_format == nullptr || src_format.bits_per_pixel == 0 ||
          src_format.frames == nullptr || src_format.frame_count == 0)
      {
        return ErrorCode::ARG_ERR;
      }
      if (frame_count_ + src_format.frame_count > MAX_FRAMES)
      {
        return ErrorCode::NO_MEM;
      }

      FormatRuntime& dst_format = formats_[format_count_];
      LibXR::Memory::FastCopy(dst_format.guid_format, src_format.guid_format,
                              sizeof(dst_format.guid_format));
      dst_format.bits_per_pixel = src_format.bits_per_pixel;
      dst_format.first_frame = frame_count_;
      dst_format.frame_count = src_format.frame_count;
      dst_format.default_frame_index = src_format.default_frame_index;
      if (dst_format.default_frame_index == 0 ||
          dst_format.default_frame_index > src_format.frame_count)
      {
        dst_format.default_frame_index = 1;
      }

      for (uint8_t j = 0; j < src_format.frame_count; ++j)
      {
        const FrameSpec& src_frame = src_format.frames[j];
        if (src_frame.width == 0 || src_frame.height == 0 ||
            src_frame.frame_intervals_100ns == nullptr ||
            src_frame.frame_interval_count == 0 ||
            src_frame.frame_interval_count > MAX_INTERVALS_PER_FRAME)
        {
          return ErrorCode::ARG_ERR;
        }

        FrameRuntime& dst_frame = frames_[frame_count_];
        dst_frame.format_index = static_cast<uint8_t>(format_count_ + 1);
        dst_frame.frame_index = static_cast<uint8_t>(j + 1);
        dst_frame.width = src_frame.width;
        dst_frame.height = src_frame.height;
        dst_frame.interval_count = src_frame.frame_interval_count;
        for (uint8_t k = 0; k < src_frame.frame_interval_count; ++k)
        {
          dst_frame.intervals[k] = src_frame.frame_intervals_100ns[k];
        }
        dst_frame.default_interval = src_frame.default_frame_interval_100ns;
        if (dst_frame.default_interval == 0 ||
            !FrameHasInterval(dst_frame, dst_frame.default_interval))
        {
          dst_frame.default_interval = dst_frame.intervals[0];
        }
        dst_frame.max_frame_buffer_size = src_frame.max_frame_buffer_size;
        if (dst_frame.max_frame_buffer_size == 0)
        {
          dst_frame.max_frame_buffer_size =
              static_cast<uint32_t>(dst_frame.width) * dst_frame.height *
              dst_format.bits_per_pixel / 8u;
        }
        ++frame_count_;
      }
      ++format_count_;
    }
    return ErrorCode::OK;
  }

  void SelectDefaultMode()
  {
    const FormatRuntime& format = formats_[0];
    const FrameRuntime& frame =
        frames_[format.first_frame + static_cast<size_t>(format.default_frame_index - 1)];
    committed_ = BuildControl(1, frame.frame_index, frame.default_interval);
    pending_ = committed_;
  }

  static bool FrameHasInterval(const FrameRuntime& frame, uint32_t interval)
  {
    for (uint8_t i = 0; i < frame.interval_count; ++i)
    {
      if (frame.intervals[i] == interval)
      {
        return true;
      }
    }
    return false;
  }

  const FrameRuntime* FindFrame(uint8_t format_index, uint8_t frame_index) const
  {
    if (format_index == 0 || format_index > format_count_)
    {
      return nullptr;
    }
    const FormatRuntime& format = formats_[format_index - 1];
    if (frame_index == 0 || frame_index > format.frame_count)
    {
      return nullptr;
    }
    return &frames_[format.first_frame + static_cast<size_t>(frame_index - 1)];
  }

  ProbeCommitControl BuildControl(uint8_t format_index, uint8_t frame_index,
                                  uint32_t interval) const
  {
    ProbeCommitControl control{};
    const FrameRuntime* frame = FindFrame(format_index, frame_index);
    if (frame == nullptr)
    {
      frame = &frames_[0];
      format_index = 1;
      frame_index = 1;
    }
    if (!FrameHasInterval(*frame, interval))
    {
      interval = frame->default_interval;
    }

    control.bFormatIndex = format_index;
    control.bFrameIndex = frame_index;
    control.dwFrameInterval = interval;
    control.dwMaxVideoFrameSize = frame->max_frame_buffer_size;
    control.dwMaxPayloadTransferSize = iso_in_max_packet_size_;
    return control;
  }

  bool NormalizeControl(const ProbeCommitControl& src, ProbeCommitControl& dst) const
  {
    uint8_t format_index = src.bFormatIndex;
    uint8_t frame_index = src.bFrameIndex;
    const FrameRuntime* frame = FindFrame(format_index, frame_index);
    if (frame == nullptr)
    {
      format_index = committed_.bFormatIndex;
      frame_index = committed_.bFrameIndex;
      frame = FindFrame(format_index, frame_index);
    }
    if (frame == nullptr)
    {
      return false;
    }

    uint32_t interval = src.dwFrameInterval;
    if (!FrameHasInterval(*frame, interval))
    {
      interval = frame->default_interval;
    }
    dst = BuildControl(format_index, frame_index, interval);
    return true;
  }

  void Put8(size_t& index, uint8_t value)
  {
    ASSERT(index < sizeof(descriptor_buffer_));
    descriptor_buffer_[index++] = value;
  }

  void Put16(size_t& index, uint16_t value)
  {
    Put8(index, static_cast<uint8_t>(value & 0xFF));
    Put8(index, static_cast<uint8_t>((value >> 8) & 0xFF));
  }

  void Put32(size_t& index, uint32_t value)
  {
    Put16(index, static_cast<uint16_t>(value & 0xFFFF));
    Put16(index, static_cast<uint16_t>((value >> 16) & 0xFFFF));
  }

  template <typename T>
  void PutStruct(size_t& index, const T& value)
  {
    ASSERT(index + sizeof(T) <= sizeof(descriptor_buffer_));
    LibXR::Memory::FastCopy(descriptor_buffer_ + index, &value, sizeof(T));
    index += sizeof(T);
  }

  size_t BuildDescriptors()
  {
    size_t index = 0;

    IADDescriptor iad{8,
                      static_cast<uint8_t>(DescriptorType::IAD),
                      itf_vc_num_,
                      2,
                      USB_CLASS_VIDEO,
                      SUBCLASS_VC,
                      0x00,
                      0};
    PutStruct(index, iad);

    InterfaceDescriptor vc_intf{9,
                                static_cast<uint8_t>(DescriptorType::INTERFACE),
                                itf_vc_num_,
                                0,
                                0,
                                USB_CLASS_VIDEO,
                                SUBCLASS_VC,
                                0x00,
                                GetInterfaceStringIndex(0u)};
    PutStruct(index, vc_intf);

    VCHeader vc_header{};
    vc_header.baInterfaceNr = itf_vs_num_;
    vc_header.wTotalLength = static_cast<uint16_t>(
        sizeof(VCHeader) + sizeof(CameraInputTerminal) + sizeof(OutputTerminal));
    PutStruct(index, vc_header);
    PutStruct(index, CameraInputTerminal{});
    PutStruct(index, OutputTerminal{});

    InterfaceDescriptor vs_alt0{9,
                                static_cast<uint8_t>(DescriptorType::INTERFACE),
                                itf_vs_num_,
                                0,
                                0,
                                USB_CLASS_VIDEO,
                                SUBCLASS_VS,
                                0x00,
                                GetInterfaceStringIndex(1u)};
    PutStruct(index, vs_alt0);

    const size_t vs_input_header_index = index;
    Put8(index, static_cast<uint8_t>(13 + format_count_));
    Put8(index, CS_INTERFACE);
    Put8(index, VS_INPUT_HEADER);
    Put8(index, format_count_);
    const size_t vs_total_len_index = index;
    Put16(index, 0);
    Put8(index, ep_iso_in_addr_);
    Put8(index, 0);
    Put8(index, ID_USB_OT);
    Put8(index, 0);
    Put8(index, 0);
    Put8(index, 0);
    Put8(index, 1);
    for (uint8_t i = 0; i < format_count_; ++i)
    {
      Put8(index, 0);
    }

    for (uint8_t i = 0; i < format_count_; ++i)
    {
      const FormatRuntime& format = formats_[i];
      VSFormatUncompressed fmt{};
      fmt.bFormatIndex = static_cast<uint8_t>(i + 1);
      fmt.bNumFrameDescriptors = format.frame_count;
      LibXR::Memory::FastCopy(fmt.guidFormat, format.guid_format, sizeof(fmt.guidFormat));
      fmt.bBitsPerPixel = format.bits_per_pixel;
      fmt.bDefaultFrameIndex = format.default_frame_index;
      PutStruct(index, fmt);

      for (uint8_t j = 0; j < format.frame_count; ++j)
      {
        const FrameRuntime& frame = frames_[format.first_frame + j];
        Put8(index, static_cast<uint8_t>(26 + 4 * frame.interval_count));
        Put8(index, CS_INTERFACE);
        Put8(index, VS_FRAME_UNCOMPRESSED);
        Put8(index, frame.frame_index);
        Put8(index, 0);
        Put16(index, frame.width);
        Put16(index, frame.height);
        const uint32_t min_interval = frame.intervals[frame.interval_count - 1];
        const uint32_t max_interval = frame.intervals[0];
        const uint32_t min_bit_rate =
            FrameBitRate(format.bits_per_pixel, frame.width, frame.height, max_interval);
        const uint32_t max_bit_rate =
            FrameBitRate(format.bits_per_pixel, frame.width, frame.height, min_interval);
        Put32(index, min_bit_rate);
        Put32(index, max_bit_rate);
        Put32(index, frame.max_frame_buffer_size);
        Put32(index, frame.default_interval);
        Put8(index, frame.interval_count);
        for (uint8_t k = 0; k < frame.interval_count; ++k)
        {
          Put32(index, frame.intervals[k]);
        }
      }
    }

    PutStruct(index, VSColorMatching{});

    const uint16_t vs_total_len =
        static_cast<uint16_t>(index - vs_input_header_index);
    descriptor_buffer_[vs_total_len_index] = static_cast<uint8_t>(vs_total_len & 0xFF);
    descriptor_buffer_[vs_total_len_index + 1] =
        static_cast<uint8_t>((vs_total_len >> 8) & 0xFF);

    InterfaceDescriptor vs_alt1{9,
                                static_cast<uint8_t>(DescriptorType::INTERFACE),
                                itf_vs_num_,
                                1,
                                1,
                                USB_CLASS_VIDEO,
                                SUBCLASS_VS,
                                0x00,
                                GetInterfaceStringIndex(1u)};
    PutStruct(index, vs_alt1);

    EndpointDescriptor ep{7,
                          static_cast<uint8_t>(DescriptorType::ENDPOINT),
                          ep_iso_in_addr_,
                          0x05,
                          iso_in_max_packet_size_,
                          (speed_ == Speed::HIGH) ? interval_ : static_cast<uint8_t>(1)};
    PutStruct(index, ep);

    return index;
  }

  static uint32_t FrameBitRate(uint8_t bits_per_pixel, uint16_t width, uint16_t height,
                               uint32_t interval_100ns)
  {
    if (interval_100ns == 0)
    {
      return 0;
    }
    const uint64_t bits = static_cast<uint64_t>(width) * height * bits_per_pixel;
    return static_cast<uint32_t>((bits * 10000000ULL) / interval_100ns);
  }

  static void OnInCompleteStatic(bool in_isr, UVCCamera* self, ConstRawData& data)
  {
    if (self->inited_)
    {
      self->OnInComplete(in_isr, data);
    }
  }

  void OnInComplete(bool, ConstRawData& data)
  {
    if (pending_payload_ > 0 && data.size_ > 0)
    {
      active_frame_offset_ += pending_payload_;
      if (pending_eof_)
      {
        frame_active_ = false;
        frame_id_toggle_pending_ = true;
      }
      pending_payload_ = 0;
      pending_eof_ = false;
    }
    else if (data.size_ == 0)
    {
      pending_payload_ = 0;
      pending_eof_ = false;
    }

    if (streaming_)
    {
      KickPacket();
    }
  }

  void FillPayloadHeader(uint8_t* out, bool eof) const
  {
    constexpr uint8_t HEADER_SIZE = 2;
    out[0] = HEADER_SIZE;
    out[1] = static_cast<uint8_t>(PAYLOAD_HEADER_EOH |
                                  ((frame_id_ & 0x01) ? PAYLOAD_HEADER_FID : 0x00) |
                                  (eof ? PAYLOAD_HEADER_EOF : 0x00));
  }

  void KickPacket()
  {
    if (!streaming_ || ep_iso_in_ == nullptr ||
        ep_iso_in_->GetState() != Endpoint::State::IDLE)
    {
      return;
    }

    constexpr uint8_t HEADER_SIZE = 2;
    if (iso_in_max_packet_size_ <= HEADER_SIZE)
    {
      frame_active_ = false;
      return;
    }

    auto buf = ep_iso_in_->GetBuffer();
    if (buf.size_ < HEADER_SIZE)
    {
      frame_active_ = false;
      return;
    }

    auto* out = reinterpret_cast<uint8_t*>(buf.addr_);
    if (!frame_active_)
    {
      pending_payload_ = 0;
      pending_eof_ = false;
      return;
    }

    size_t payload_capacity = iso_in_max_packet_size_ - HEADER_SIZE;
    if (payload_capacity + HEADER_SIZE > buf.size_)
    {
      payload_capacity = buf.size_ - HEADER_SIZE;
    }

    const size_t remaining = active_frame_.size_ - active_frame_offset_;
    const size_t payload = (remaining < payload_capacity) ? remaining : payload_capacity;
    const bool eof = (payload == remaining);

    if (active_frame_offset_ == 0 && frame_id_toggle_pending_)
    {
      frame_id_ ^= 0x01;
      frame_id_toggle_pending_ = false;
    }

    FillPayloadHeader(out, eof);
    LibXR::Memory::FastCopy(out + HEADER_SIZE,
                            reinterpret_cast<const uint8_t*>(active_frame_.addr_) +
                                active_frame_offset_,
                            payload);

    pending_payload_ = payload;
    pending_eof_ = eof;
    ep_iso_in_->Transfer(payload + HEADER_SIZE);
  }

  Speed speed_;
  uint8_t interval_;
  uint16_t iso_in_max_packet_size_;
  Endpoint::EPNumber iso_in_ep_num_;
  const char* control_interface_string_ = nullptr;
  const char* streaming_interface_string_ = nullptr;

  static constexpr size_t K_MAX_CONFIG_SIZE =
      8 + 9 + sizeof(VCHeader) + sizeof(CameraInputTerminal) + sizeof(OutputTerminal) + 9 +
      (13 + MAX_FORMATS) + MAX_FORMATS * sizeof(VSFormatUncompressed) +
      MAX_FRAMES * (26 + 4 * MAX_INTERVALS_PER_FRAME) + sizeof(VSColorMatching) + 9 +
      sizeof(EndpointDescriptor);

  Endpoint* ep_iso_in_ = nullptr;
  uint8_t itf_vc_num_ = 0;
  uint8_t itf_vs_num_ = 0;
  uint8_t ep_iso_in_addr_ = 0;
  bool inited_ = false;
  bool streaming_ = false;

  uint8_t descriptor_buffer_[K_MAX_CONFIG_SIZE] = {};

  FormatRuntime formats_[MAX_FORMATS] = {};
  FrameRuntime frames_[MAX_FRAMES] = {};
  uint8_t format_count_ = 0;
  uint8_t frame_count_ = 0;

  ProbeCommitControl setup_control_{};
  ProbeCommitControl pending_{};
  ProbeCommitControl committed_{};
  ProbeCommitControl control_response_{};
  uint8_t setup_selector_ = 0;
  uint8_t info_response_ = 0;
  uint8_t len_response_[2] = {0, 0};

  ConstRawData active_frame_{nullptr, 0};
  size_t active_frame_offset_ = 0;
  size_t pending_payload_ = 0;
  bool frame_active_ = false;
  bool pending_eof_ = false;
  bool frame_id_toggle_pending_ = false;
  uint8_t frame_id_ = 0;

  LibXR::Callback<LibXR::ConstRawData&> on_in_complete_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnInCompleteStatic, this);
};

}  // namespace LibXR::USB
