#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

#include "can.hpp"  // LibXR::CAN / LibXR::FDCAN (also pulls in LibXR::Database via libxr.hpp)
#include "dev_core.hpp"
#include "gpio.hpp"
#include "gs_usb_protocol.hpp"
#include "libxr_def.hpp"
#include "timebase.hpp"
#include "usb/core/desc_cfg.hpp"

namespace LibXR::USB
{

/**
 * @brief GsUsb 设备类，实现 Linux gs_usb 协议（经典 CAN + CAN FD）
 *
 *
 * @tparam CanChNum      编译期固定的 CAN 通道数
 *
 */
template <std::size_t CanChNum>
class GsUsbClass : public DeviceClass
{
  static_assert(CanChNum > 0 && CanChNum <= 255, "CanChNum must be in (0, 255]");
  static constexpr uint8_t CAN_CH_NUM = static_cast<uint8_t>(CanChNum);

  // ===== Linux gs_usb 的线缆格式（header 固定 12 字节）=====
#pragma pack(push, 1)
  struct WireHeader
  {
    uint32_t echo_id;  // u32（Linux 里是 u32）
    uint32_t can_id;   // __le32（Linux 里标注 le，但常见设备/主机均小端）
    uint8_t can_dlc;
    uint8_t channel;
    uint8_t flags;
    uint8_t reserved;
  };
#pragma pack(pop)

  static constexpr uint32_t ECHO_ID_RX = 0xFFFFFFFFu;

  static constexpr std::size_t WIRE_HDR_SIZE = sizeof(WireHeader);     // 12
  static constexpr std::size_t WIRE_CLASSIC_DATA_SIZE = 8;             // classic_can
  static constexpr std::size_t WIRE_FD_DATA_SIZE = 64;                 // canfd
  static constexpr std::size_t WIRE_TS_SIZE = 4;                       // timestamp_us
  static constexpr std::size_t WIRE_CLASSIC_SIZE = WIRE_HDR_SIZE + 8;  // 20
  static constexpr std::size_t WIRE_CLASSIC_TS_SIZE = WIRE_HDR_SIZE + 8 + 4;  // 24
  static constexpr std::size_t WIRE_FD_SIZE = WIRE_HDR_SIZE + 64;             // 76
  static constexpr std::size_t WIRE_FD_TS_SIZE = WIRE_HDR_SIZE + 64 + 4;      // 80
  static constexpr std::size_t WIRE_MAX_SIZE = WIRE_FD_TS_SIZE;               // 80

 public:
  // ============== 构造：经典 CAN（每通道 termination GPIO） ==============
  GsUsbClass(std::initializer_list<LibXR::CAN *> cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP1,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP2,
             LibXR::GPIO *identify_gpio = nullptr,
             std::initializer_list<LibXR::GPIO *> termination_gpios = {},
             LibXR::Database *database = nullptr)
      : data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        identify_gpio_(identify_gpio),
        database_(database)
  {
    ASSERT(cans.size() == CAN_CH_NUM);
    std::size_t i = 0;
    for (auto *p : cans)
    {
      cans_[i++] = p;
    }

    if (termination_gpios.size() == 0)
    {
      termination_gpios_.fill(nullptr);
    }
    else
    {
      ASSERT(termination_gpios.size() == CAN_CH_NUM);
      i = 0;
      for (auto *g : termination_gpios)
      {
        termination_gpios_[i++] = g;
      }
    }

    InitDeviceConfigClassic();
  }

  // ============== 构造：FDCAN（每通道 termination GPIO） ==============
  GsUsbClass(std::initializer_list<LibXR::FDCAN *> fd_cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
             LibXR::GPIO *identify_gpio = nullptr,
             std::initializer_list<LibXR::GPIO *> termination_gpios = {},
             LibXR::Database *database = nullptr)
      : fd_supported_(true),
        data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        identify_gpio_(identify_gpio),
        database_(database)
  {
    ASSERT(fd_cans.size() == CAN_CH_NUM);
    std::size_t i = 0;
    for (auto *p : fd_cans)
    {
      fdcans_[i] = p;
      cans_[i] = p;  // 向上转 CAN*
      ++i;
    }

    if (termination_gpios.size() == 0)
    {
      termination_gpios_.fill(nullptr);
    }
    else
    {
      ASSERT(termination_gpios.size() == CAN_CH_NUM);
      i = 0;
      for (auto *g : termination_gpios)
      {
        termination_gpios_[i++] = g;
      }
    }

    InitDeviceConfigFd();
  }

  bool IsHostFormatOK() const { return host_format_ok_; }

 protected:
  void Init(EndpointPool &endpoint_pool, uint8_t start_itf_num) override
  {
    inited_ = false;
    interface_num_ = start_itf_num;

    auto ans = endpoint_pool.Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
    ASSERT(ans == ErrorCode::OK);

    ans = endpoint_pool.Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
    ASSERT(ans == ErrorCode::OK);

    // UINT16_MAX 只是上限，底层会选不超过该值的可用最大长度
    ep_data_in_->Configure(
        {Endpoint::Direction::IN, Endpoint::Type::BULK, UINT16_MAX, true});
    ep_data_out_->Configure(
        {Endpoint::Direction::OUT, Endpoint::Type::BULK, UINT16_MAX, true});

    desc_block_.intf = {9,
                        static_cast<uint8_t>(DescriptorType::INTERFACE),
                        interface_num_,
                        0,
                        2,
                        0xFF,
                        0xFF,
                        0xFF,
                        0};

    desc_block_.ep_out = {7,
                          static_cast<uint8_t>(DescriptorType::ENDPOINT),
                          static_cast<uint8_t>(ep_data_out_->GetAddress()),
                          static_cast<uint8_t>(Endpoint::Type::BULK),
                          ep_data_out_->MaxPacketSize(),
                          0};

    desc_block_.ep_in = {7,
                         static_cast<uint8_t>(DescriptorType::ENDPOINT),
                         static_cast<uint8_t>(ep_data_in_->GetAddress()),
                         static_cast<uint8_t>(Endpoint::Type::BULK),
                         ep_data_in_->MaxPacketSize(),
                         0};

    SetData(RawData{reinterpret_cast<uint8_t *>(&desc_block_), sizeof(desc_block_)});

    ep_data_out_->SetOnTransferCompleteCallback(on_data_out_cb_);
    ep_data_in_->SetOnTransferCompleteCallback(on_data_in_cb_);

    host_format_ok_ = false;

    for (uint8_t i = 0; i < can_count_; ++i)
    {
      can_enabled_[i] = false;
      berr_enabled_[i] = false;
      fd_enabled_[i] = false;
      term_state_[i] = GsUsb::TerminationState::OFF;
      timestamps_enabled_ch_[i] = false;
    }

    // 注册 CAN RX 回调
    if (!can_rx_registered_)
    {
      for (uint8_t ch = 0; ch < can_count_; ++ch)
      {
        if (!cans_[ch]) continue;

        can_rx_ctx_[ch].self = this;
        can_rx_ctx_[ch].ch = ch;
        can_rx_cb_[ch] = LibXR::CAN::Callback::Create(OnCanRxStatic, &can_rx_ctx_[ch]);

        cans_[ch]->Register(can_rx_cb_[ch], LibXR::CAN::Type::STANDARD);
        cans_[ch]->Register(can_rx_cb_[ch], LibXR::CAN::Type::EXTENDED);
        cans_[ch]->Register(can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_STANDARD);
        cans_[ch]->Register(can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_EXTENDED);
        cans_[ch]->Register(can_rx_cb_[ch], LibXR::CAN::Type::ERROR);
      }
      can_rx_registered_ = true;
    }

    if (fd_supported_ && !fd_can_rx_registered_)
    {
      for (uint8_t ch = 0; ch < can_count_; ++ch)
      {
        if (!fdcans_[ch]) continue;

        fd_can_rx_ctx_[ch].self = this;
        fd_can_rx_ctx_[ch].ch = ch;
        fd_can_rx_cb_[ch] =
            LibXR::FDCAN::CallbackFD::Create(OnFdCanRxStatic, &fd_can_rx_ctx_[ch]);

        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::STANDARD);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::EXTENDED);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_STANDARD);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_EXTENDED);
      }
      fd_can_rx_registered_ = true;
    }

    inited_ = true;
    MaybeArmOutTransfer();
  }

  void Deinit(EndpointPool &endpoint_pool) override
  {
    inited_ = false;
    host_format_ok_ = false;

    for (uint8_t i = 0; i < can_count_; ++i)
    {
      can_enabled_[i] = false;
      berr_enabled_[i] = false;
      fd_enabled_[i] = false;
      term_state_[i] = GsUsb::TerminationState::OFF;
      timestamps_enabled_ch_[i] = false;
    }

    if (ep_data_in_)
    {
      ep_data_in_->Close();
      ep_data_in_->SetActiveLength(0);
      endpoint_pool.Release(ep_data_in_);
      ep_data_in_ = nullptr;
    }

    if (ep_data_out_)
    {
      ep_data_out_->Close();
      ep_data_out_->SetActiveLength(0);
      endpoint_pool.Release(ep_data_out_);
      ep_data_out_ = nullptr;
    }
  }

  size_t GetInterfaceNum() override { return 1; }
  bool HasIAD() override { return false; }

  bool OwnsEndpoint(uint8_t ep_addr) const override
  {
    if (!inited_) return false;

    return (ep_data_in_ && ep_data_in_->GetAddress() == ep_addr) ||
           (ep_data_out_ && ep_data_out_->GetAddress() == ep_addr);
  }

  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }

  ErrorCode OnClassRequest(bool, uint8_t, uint16_t, uint16_t, uint16_t,
                           DeviceClass::RequestResult &) override
  {
    return ErrorCode::NOT_SUPPORT;
  }

  ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                            uint16_t wLength, uint16_t wIndex,
                            DeviceClass::RequestResult &result) override
  {
    UNUSED(in_isr);
    UNUSED(wIndex);

    auto req = static_cast<GsUsb::BReq>(bRequest);

    switch (req)
    {
      // ===== Device -> Host =====
      case GsUsb::BReq::BT_CONST:
      {
        if (wLength < sizeof(bt_const_)) return ErrorCode::ARG_ERR;
        result.write_data = ConstRawData{reinterpret_cast<const uint8_t *>(&bt_const_),
                                         sizeof(bt_const_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BT_CONST_EXT:
      {
        if (!fd_supported_) return ErrorCode::NOT_SUPPORT;
        if (wLength < sizeof(bt_const_ext_)) return ErrorCode::ARG_ERR;
        result.write_data = ConstRawData{
            reinterpret_cast<const uint8_t *>(&bt_const_ext_), sizeof(bt_const_ext_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::DEVICE_CONFIG:
      {
        if (wLength < sizeof(dev_cfg_)) return ErrorCode::ARG_ERR;
        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&dev_cfg_), sizeof(dev_cfg_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::TIMESTAMP:
      {
        // Linux gs_usb：这是设备级 32-bit us 计数器读取（wValue/wIndex = 0）
        ctrl_buf_.timestamp_us = MakeTimestampUsGlobal();
        if (wLength < sizeof(ctrl_buf_.timestamp_us)) return ErrorCode::ARG_ERR;

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.timestamp_us),
                         sizeof(ctrl_buf_.timestamp_us)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_TERMINATION:
      {
        if (wValue >= can_count_) return ErrorCode::ARG_ERR;
        ctrl_buf_.term.state = static_cast<uint32_t>(term_state_[wValue]);
        if (wLength < sizeof(ctrl_buf_.term)) return ErrorCode::ARG_ERR;

        result.write_data = ConstRawData{
            reinterpret_cast<const uint8_t *>(&ctrl_buf_.term), sizeof(ctrl_buf_.term)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_STATE:
      {
        if (wValue >= can_count_) return ErrorCode::ARG_ERR;

        GsUsb::CanState st = GsUsb::CanState::ERROR_ACTIVE;
        uint32_t rxerr = 0;
        uint32_t txerr = 0;

        auto *can = cans_[wValue];
        if (can != nullptr)
        {
          LibXR::CAN::ErrorState es{};
          if (can->GetErrorState(es) == ErrorCode::OK)
          {
            if (es.bus_off)
              st = GsUsb::CanState::BUS_OFF;
            else if (es.error_passive)
              st = GsUsb::CanState::ERROR_PASSIVE;
            else if (es.error_warning)
              st = GsUsb::CanState::ERROR_WARNING;
            else
              st = GsUsb::CanState::ERROR_ACTIVE;

            rxerr = es.rx_error_counter;
            txerr = es.tx_error_counter;
          }
        }

        ctrl_buf_.dev_state.state = static_cast<uint32_t>(st);
        ctrl_buf_.dev_state.rxerr = rxerr;
        ctrl_buf_.dev_state.txerr = txerr;

        if (wLength < sizeof(ctrl_buf_.dev_state)) return ErrorCode::ARG_ERR;

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.dev_state),
                         sizeof(ctrl_buf_.dev_state)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_USER_ID:
      {
        ctrl_buf_.user_id = GetUserIdFromStorage();
        if (wLength < sizeof(ctrl_buf_.user_id)) return ErrorCode::ARG_ERR;

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.user_id),
                         sizeof(ctrl_buf_.user_id)};
        return ErrorCode::OK;
      }

      // ===== Host -> Device（有 DATA 阶段） =====
      case GsUsb::BReq::HOST_FORMAT:
      {
        if (wLength != sizeof(GsUsb::HostConfig)) return ErrorCode::ARG_ERR;
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.host_cfg),
                                   sizeof(GsUsb::HostConfig)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BITTIMING:
      {
        if (wLength != sizeof(GsUsb::DeviceBitTiming) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.bt),
                                   sizeof(GsUsb::DeviceBitTiming)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::DATA_BITTIMING:
      {
        if (!fd_supported_) return ErrorCode::NOT_SUPPORT;
        if (wLength != sizeof(GsUsb::DeviceBitTiming) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.bt),
                                   sizeof(GsUsb::DeviceBitTiming)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::MODE:
      {
        if (wLength != sizeof(GsUsb::DeviceMode) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.mode),
                                   sizeof(GsUsb::DeviceMode)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BERR:
      {
        if (wLength != sizeof(uint32_t) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data =
            RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.berr_on), sizeof(uint32_t)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::IDENTIFY:
      {
        if (wLength != sizeof(GsUsb::Identify) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.identify),
                                   sizeof(GsUsb::Identify)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::SET_TERMINATION:
      {
        if (wLength != sizeof(GsUsb::DeviceTerminationState) || wValue >= can_count_)
          return ErrorCode::ARG_ERR;
        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.term),
                                   sizeof(GsUsb::DeviceTerminationState)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::SET_USER_ID:
      {
        if (wLength != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
        result.read_data =
            RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.user_id), sizeof(uint32_t)};
        return ErrorCode::OK;
      }

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData &data) override
  {
    UNUSED(in_isr);

    auto req = static_cast<GsUsb::BReq>(bRequest);

    switch (req)
    {
      case GsUsb::BReq::HOST_FORMAT:
        if (data.size_ != sizeof(GsUsb::HostConfig)) return ErrorCode::ARG_ERR;
        return HandleHostFormat(ctrl_buf_.host_cfg);

      case GsUsb::BReq::BITTIMING:
        if (data.size_ != sizeof(GsUsb::DeviceBitTiming)) return ErrorCode::ARG_ERR;
        return HandleBitTiming(ctrl_target_channel_, ctrl_buf_.bt);

      case GsUsb::BReq::DATA_BITTIMING:
        if (!fd_supported_) return ErrorCode::NOT_SUPPORT;
        if (data.size_ != sizeof(GsUsb::DeviceBitTiming)) return ErrorCode::ARG_ERR;
        return HandleDataBitTiming(ctrl_target_channel_, ctrl_buf_.bt);

      case GsUsb::BReq::MODE:
        if (data.size_ != sizeof(GsUsb::DeviceMode)) return ErrorCode::ARG_ERR;
        return HandleMode(ctrl_target_channel_, ctrl_buf_.mode);

      case GsUsb::BReq::BERR:
        if (data.size_ != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
        return HandleBerr(ctrl_target_channel_, ctrl_buf_.berr_on);

      case GsUsb::BReq::IDENTIFY:
        if (data.size_ != sizeof(GsUsb::Identify)) return ErrorCode::ARG_ERR;
        return HandleIdentify(ctrl_target_channel_, ctrl_buf_.identify);

      case GsUsb::BReq::SET_TERMINATION:
        if (data.size_ != sizeof(GsUsb::DeviceTerminationState))
          return ErrorCode::ARG_ERR;
        return HandleSetTermination(ctrl_target_channel_, ctrl_buf_.term);

      case GsUsb::BReq::SET_USER_ID:
        if (data.size_ != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
        SetUserIdToStorage(ctrl_buf_.user_id);
        return ErrorCode::OK;

      case GsUsb::BReq::DEVICE_CONFIG:
      case GsUsb::BReq::BT_CONST:
      case GsUsb::BReq::BT_CONST_EXT:
      case GsUsb::BReq::TIMESTAMP:
      case GsUsb::BReq::GET_TERMINATION:
      case GsUsb::BReq::GET_STATE:
      case GsUsb::BReq::GET_USER_ID:
        return ErrorCode::OK;

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  // ================= Bulk 端点回调 =================
  static void OnDataOutCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_) return;
    self->OnDataOutComplete(in_isr, data);
  }

  static void OnDataInCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_) return;
    self->OnDataInComplete(in_isr, data);
  }

  void OnDataOutComplete(bool in_isr, ConstRawData &data)
  {
    UNUSED(in_isr);

    if (!ep_data_out_) return;

    const std::size_t rxlen = data.size_;
    if (rxlen < WIRE_CLASSIC_SIZE)
    {
      MaybeArmOutTransfer();
      return;
    }

    // 解析 wire header（避免未对齐）
    WireHeader wh{};
    Memory::FastCopy(&wh, data.addr_, std::min<std::size_t>(sizeof(wh), rxlen));

    const uint8_t ch = wh.channel;
    if (ch >= can_count_ || !cans_[ch])
    {
      MaybeArmOutTransfer();
      return;
    }

    const bool is_fd = (wh.flags & GsUsb::CAN_FLAG_FD) != 0;
    const uint8_t *payload =
        reinterpret_cast<const uint8_t *>(data.addr_) + WIRE_HDR_SIZE;

    if (is_fd)
    {
      if (!fd_supported_ || !fdcans_[ch] || !fd_enabled_[ch])
      {
        MaybeArmOutTransfer();
        return;
      }

      if (rxlen < WIRE_FD_SIZE)
      {
        MaybeArmOutTransfer();
        return;
      }

      LibXR::FDCAN::FDPack pack{};
      HostWireToFdPack(wh, payload, pack);
      (void)fdcans_[ch]->AddMessage(pack);
    }
    else
    {
      if (!can_enabled_[ch])
      {
        MaybeArmOutTransfer();
        return;
      }

      if (rxlen < WIRE_CLASSIC_SIZE)
      {
        MaybeArmOutTransfer();
        return;
      }

      LibXR::CAN::ClassicPack pack{};
      HostWireToClassicPack(wh, payload, pack);
      (void)cans_[ch]->AddMessage(pack);
    }

    // TX echo：Host 通过 echo_id 跟踪 TX buffer；设备需回送 echo_id
    if (wh.echo_id != ECHO_ID_RX)
    {
      QueueItem qi{};
      qi.hdr = wh;
      qi.is_fd = is_fd;
      qi.data_len = is_fd ? 64u : 8u;
      qi.timestamp_us = MakeTimestampUs(ch);  // 若通道未启 HW_TIMESTAMP，发送时不会附加
      Memory::FastCopy(qi.data.data(), payload, qi.data_len);
      (void)EnqueueFrame(qi, true, in_isr);
    }

    MaybeArmOutTransfer();
  }

  void OnDataInComplete(bool in_isr, ConstRawData &data)
  {
    UNUSED(in_isr);
    UNUSED(data);
    TryKickTx(false);
  }

 private:
  static constexpr size_t RX_QUEUE_SIZE = 64;
  static constexpr size_t ECHO_QUEUE_SIZE = 64;

  // ================= 成员 =================
  std::array<LibXR::CAN *, CanChNum> cans_{};
  std::array<LibXR::FDCAN *, CanChNum> fdcans_{};
  bool fd_supported_ = false;

  uint8_t can_count_ = CAN_CH_NUM;

  Endpoint::EPNumber data_in_ep_num_;
  Endpoint::EPNumber data_out_ep_num_;

  Endpoint *ep_data_in_ = nullptr;
  Endpoint *ep_data_out_ = nullptr;

  bool inited_ = false;
  uint8_t interface_num_ = 0;

  LibXR::GPIO *identify_gpio_ = nullptr;
  std::array<LibXR::GPIO *, CanChNum> termination_gpios_{};

  LibXR::Database *database_ = nullptr;
  uint32_t user_id_ram_ = 0;

  LibXR::Callback<LibXR::ConstRawData &> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData &> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataInCompleteStatic, this);

  GsUsb::DeviceConfig dev_cfg_{};
  GsUsb::DeviceBTConst bt_const_{};
  GsUsb::DeviceBTConstExtended bt_const_ext_{};

  union
  {
    GsUsb::HostConfig host_cfg;
    GsUsb::DeviceBitTiming bt;
    GsUsb::DeviceMode mode;
    uint32_t berr_on;
    GsUsb::Identify identify;
    uint32_t timestamp_us;
    GsUsb::DeviceTerminationState term;
    GsUsb::DeviceState dev_state;
    uint32_t user_id;
  } ctrl_buf_{};

  std::array<LibXR::CAN::Configuration, CanChNum> config_{};
  std::array<LibXR::FDCAN::Configuration, CanChNum> fd_config_{};

  bool host_format_ok_ = false;
  bool can_enabled_[CanChNum] = {false};
  bool berr_enabled_[CanChNum] = {false};
  bool fd_enabled_[CanChNum] = {false};

  // HW_TIMESTAMP：按通道 enable（MODE.flags & HW_TIMESTAMP）
  bool timestamps_enabled_ch_[CanChNum] = {false};

  GsUsb::TerminationState term_state_[CanChNum] = {GsUsb::TerminationState::OFF};
  uint8_t ctrl_target_channel_ = 0;

#pragma pack(push, 1)
  struct GsUsbDescBlock
  {
    InterfaceDescriptor intf;
    EndpointDescriptor ep_out;
    EndpointDescriptor ep_in;
  } desc_block_{};
#pragma pack(pop)

  // OUT 接收缓冲区（最大 80）
  uint8_t rx_buf_[WIRE_MAX_SIZE]{};

  // IN 发送 staging（TransferMultiBulk 期间必须有效）
  std::array<uint8_t, WIRE_MAX_SIZE> tx_buf_{};

  // ================= CAN RX 回调 & BULK IN 发送队列 =================
  struct CanRxCtx
  {
    GsUsbClass *self;
    uint8_t ch;
  };
  struct FdCanRxCtx
  {
    GsUsbClass *self;
    uint8_t ch;
  };

  bool can_rx_registered_ = false;
  CanRxCtx can_rx_ctx_[CanChNum]{};
  LibXR::CAN::Callback can_rx_cb_[CanChNum]{};

  bool fd_can_rx_registered_ = false;
  FdCanRxCtx fd_can_rx_ctx_[CanChNum]{};
  LibXR::FDCAN::CallbackFD fd_can_rx_cb_[CanChNum]{};

  static void OnCanRxStatic(bool in_isr, CanRxCtx *ctx,
                            const LibXR::CAN::ClassicPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_) return;
    ctx->self->OnCanRx(in_isr, ctx->ch, pack);
  }

  static void OnFdCanRxStatic(bool in_isr, FdCanRxCtx *ctx,
                              const LibXR::FDCAN::FDPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_) return;
    ctx->self->OnFdCanRx(in_isr, ctx->ch, pack);
  }

  // ====== 队列元素：独立保存 timestamp_us（用于 *_ts 变体）======
  struct QueueItem
  {
    WireHeader hdr{};
    bool is_fd = false;
    uint8_t data_len = 0;  // 8 或 64
    uint32_t timestamp_us = 0;
    std::array<uint8_t, 64> data{};
  };

  LibXR::LockFreeQueue<QueueItem> rx_queue_{RX_QUEUE_SIZE};
  LibXR::LockFreeQueue<QueueItem> echo_queue_{ECHO_QUEUE_SIZE};

  void OnCanRx(bool in_isr, uint8_t ch, const LibXR::CAN::ClassicPack &pack)
  {
    if (ch >= can_count_ || !ep_data_in_) return;

    if (pack.type == LibXR::CAN::Type::ERROR)
    {
      QueueItem qi{};
      if (ErrorPackToHostErrorFrame(ch, pack, qi))
      {
        (void)EnqueueFrame(qi, false, in_isr);
      }
      return;
    }

    if (!can_enabled_[ch]) return;

    QueueItem qi{};
    ClassicPackToQueueItem(pack, ch, qi);
    (void)EnqueueFrame(qi, false, in_isr);
  }

  void OnFdCanRx(bool in_isr, uint8_t ch, const LibXR::FDCAN::FDPack &pack)
  {
    if (!fd_supported_ || ch >= can_count_ || !fd_enabled_[ch] || !ep_data_in_) return;

    QueueItem qi{};
    FdPackToQueueItem(pack, ch, qi);

    // BRS/ESI：仍按配置粗略设置（此处语义不完美，按你的“只改数据库”要求保留原样）
    const auto &fd_cfg = fd_config_[ch];
    if (fd_cfg.fd_mode.brs) qi.hdr.flags |= GsUsb::CAN_FLAG_BRS;
    if (fd_cfg.fd_mode.esi) qi.hdr.flags |= GsUsb::CAN_FLAG_ESI;

    (void)EnqueueFrame(qi, false, in_isr);
  }

  bool EnqueueFrame(const QueueItem &qi, bool is_echo, bool in_isr)
  {
    UNUSED(in_isr);

    const ErrorCode ec = is_echo ? echo_queue_.Push(qi) : rx_queue_.Push(qi);
    if (ec != ErrorCode::OK) return false;

    TryKickTx(in_isr);
    MaybeArmOutTransfer();
    return true;
  }

  void TryKickTx(bool in_isr)
  {
    UNUSED(in_isr);

    if (!ep_data_in_) return;
    if (ep_data_in_->GetState() != Endpoint::State::IDLE) return;

    QueueItem qi{};
    ErrorCode ec = echo_queue_.Pop(qi);
    if (ec != ErrorCode::OK)
    {
      ec = rx_queue_.Pop(qi);
      if (ec != ErrorCode::OK) return;
    }

    const std::size_t send_len = PackQueueItemToWire(qi, tx_buf_.data(), tx_buf_.size());
    if (send_len == 0) return;

    RawData tx_raw{tx_buf_.data(), send_len};
    (void)ep_data_in_->TransferMultiBulk(tx_raw);

    MaybeArmOutTransfer();
  }

  void MaybeArmOutTransfer()
  {
    if (!ep_data_out_) return;
    if (ep_data_out_->GetState() != Endpoint::State::IDLE) return;

    // 防御：队列打满就不继续收
    if (rx_queue_.EmptySize() == 0 || echo_queue_.EmptySize() == 0) return;

    RawData rx_raw{rx_buf_, static_cast<size_t>(WIRE_MAX_SIZE)};
    (void)ep_data_out_->TransferMultiBulk(rx_raw);
  }

  // ================= 业务处理函数 =================
  bool HasAnyTerminationGpio_() const
  {
    for (uint8_t i = 0; i < can_count_; ++i)
    {
      if (termination_gpios_[i] != nullptr) return true;
    }
    return false;
  }

  void InitDeviceConfigClassic()
  {
    can_count_ = CAN_CH_NUM;
    ASSERT(can_count_ > 0);
    ASSERT(cans_[0] != nullptr);

    dev_cfg_.reserved1 = 0;
    dev_cfg_.reserved2 = 0;
    dev_cfg_.reserved3 = 0;
    dev_cfg_.icount = static_cast<uint8_t>(can_count_ - 1);
    dev_cfg_.sw_version = 2;
    dev_cfg_.hw_version = 1;

    const uint32_t fclk = cans_[0]->GetClockFreq();

    bt_const_.feature = GsUsb::CAN_FEAT_LISTEN_ONLY | GsUsb::CAN_FEAT_LOOP_BACK |
                        GsUsb::CAN_FEAT_TRIPLE_SAMPLE | GsUsb::CAN_FEAT_ONE_SHOT |
                        GsUsb::CAN_FEAT_HW_TIMESTAMP | GsUsb::CAN_FEAT_BERR_REPORTING |
                        GsUsb::CAN_FEAT_GET_STATE |
                        GsUsb::CAN_FEAT_USER_ID;  // 你实现了 GET/SET_USER_ID

    if (identify_gpio_) bt_const_.feature |= GsUsb::CAN_FEAT_IDENTIFY;
    if (HasAnyTerminationGpio_()) bt_const_.feature |= GsUsb::CAN_FEAT_TERMINATION;

    // padding 特性明确移除：不声明、不实现
    // bt_const_.feature |= GsUsb::CAN_FEAT_PAD_PKTS_TO_MAX_PKT_SIZE;

    bt_const_.fclk_can = fclk;
    bt_const_.btc.tseg1_min = 1;
    bt_const_.btc.tseg1_max = 16;
    bt_const_.btc.tseg2_min = 1;
    bt_const_.btc.tseg2_max = 8;
    bt_const_.btc.sjw_max = 4;
    bt_const_.btc.brp_min = 1;
    bt_const_.btc.brp_max = 1024;
    bt_const_.btc.brp_inc = 1;

    bt_const_ext_.feature = bt_const_.feature;
    bt_const_ext_.fclk_can = fclk;
    bt_const_ext_.btc = bt_const_.btc;
    bt_const_ext_.dbtc = bt_const_.btc;

    Memory::FastSet(config_.data(), 0, sizeof(config_));
    Memory::FastSet(fd_config_.data(), 0, sizeof(fd_config_));
  }

  void InitDeviceConfigFd()
  {
    can_count_ = CAN_CH_NUM;
    ASSERT(can_count_ > 0);
    ASSERT(cans_[0] != nullptr);

    dev_cfg_.reserved1 = 0;
    dev_cfg_.reserved2 = 0;
    dev_cfg_.reserved3 = 0;
    dev_cfg_.icount = static_cast<uint8_t>(can_count_ - 1);
    dev_cfg_.sw_version = 2;
    dev_cfg_.hw_version = 1;

    const uint32_t fclk = cans_[0]->GetClockFreq();

    bt_const_.feature = GsUsb::CAN_FEAT_LISTEN_ONLY | GsUsb::CAN_FEAT_LOOP_BACK |
                        GsUsb::CAN_FEAT_TRIPLE_SAMPLE | GsUsb::CAN_FEAT_ONE_SHOT |
                        GsUsb::CAN_FEAT_HW_TIMESTAMP | GsUsb::CAN_FEAT_BERR_REPORTING |
                        GsUsb::CAN_FEAT_GET_STATE | GsUsb::CAN_FEAT_USER_ID |
                        GsUsb::CAN_FEAT_FD | GsUsb::CAN_FEAT_BT_CONST_EXT;

    if (identify_gpio_) bt_const_.feature |= GsUsb::CAN_FEAT_IDENTIFY;
    if (HasAnyTerminationGpio_()) bt_const_.feature |= GsUsb::CAN_FEAT_TERMINATION;

    // padding 特性明确移除
    // bt_const_.feature |= GsUsb::CAN_FEAT_PAD_PKTS_TO_MAX_PKT_SIZE;

    bt_const_.fclk_can = fclk;
    bt_const_.btc.tseg1_min = 1;
    bt_const_.btc.tseg1_max = 16;
    bt_const_.btc.tseg2_min = 1;
    bt_const_.btc.tseg2_max = 8;
    bt_const_.btc.sjw_max = 4;
    bt_const_.btc.brp_min = 1;
    bt_const_.btc.brp_max = 1024;
    bt_const_.btc.brp_inc = 1;

    bt_const_ext_.feature = bt_const_.feature;
    bt_const_ext_.fclk_can = fclk;
    bt_const_ext_.btc = bt_const_.btc;
    bt_const_ext_.dbtc = bt_const_.btc;  // TODO: 真正 FD 数据相位参数

    Memory::FastSet(config_.data(), 0, sizeof(config_));
    Memory::FastSet(fd_config_.data(), 0, sizeof(fd_config_));
  }

  ErrorCode HandleHostFormat(const GsUsb::HostConfig &cfg)
  {
    // Linux 驱动：struct gs_host_config::byte_order 用 0x0000beef 标记
    // candleLight 固件不支持 host byte order；此实现也只接受小端标记
    host_format_ok_ = (cfg.byte_order == 0x0000beefu);
    return ErrorCode::OK;
  }

  ErrorCode HandleBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt)
  {
    if (!host_format_ok_ || ch >= can_count_ || !cans_[ch]) return ErrorCode::ARG_ERR;

    const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
    const uint32_t tseg2 = bt.phase_seg2;
    const uint32_t tq_num = 1u + tseg1 + tseg2;

    const uint32_t fclk = cans_[ch]->GetClockFreq();

    auto &cfg = config_[ch];
    cfg.bit_timing.brp = bt.brp;
    cfg.bit_timing.prop_seg = bt.prop_seg;
    cfg.bit_timing.phase_seg1 = bt.phase_seg1;
    cfg.bit_timing.phase_seg2 = bt.phase_seg2;
    cfg.bit_timing.sjw = bt.sjw;

    if (bt.brp != 0u && tq_num != 0u)
    {
      cfg.bitrate = fclk / (bt.brp * tq_num);
      cfg.sample_point = static_cast<float>(1u + tseg1) / static_cast<float>(tq_num);
    }
    else
    {
      cfg.bitrate = 0;
      cfg.sample_point = 0.0f;
    }

    if (fd_supported_ && fdcans_[ch])
    {
      auto &fd_cfg = fd_config_[ch];
      static_cast<CAN::Configuration &>(fd_cfg) = cfg;
    }

    return cans_[ch]->SetConfig(cfg);
  }

  ErrorCode HandleDataBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt)
  {
    if (!host_format_ok_) return ErrorCode::ARG_ERR;
    if (!fd_supported_ || ch >= can_count_ || !fdcans_[ch]) return ErrorCode::NOT_SUPPORT;

    const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
    const uint32_t tseg2 = bt.phase_seg2;
    const uint32_t tq_num = 1u + tseg1 + tseg2;

    const uint32_t fclk = fdcans_[ch]->GetClockFreq();

    auto &fd_cfg = fd_config_[ch];
    static_cast<CAN::Configuration &>(fd_cfg) = config_[ch];

    fd_cfg.data_timing.brp = bt.brp;
    fd_cfg.data_timing.prop_seg = bt.prop_seg;
    fd_cfg.data_timing.phase_seg1 = bt.phase_seg1;
    fd_cfg.data_timing.phase_seg2 = bt.phase_seg2;
    fd_cfg.data_timing.sjw = bt.sjw;

    if (bt.brp != 0u && tq_num != 0u)
    {
      fd_cfg.data_bitrate = fclk / (bt.brp * tq_num);
      fd_cfg.data_sample_point =
          static_cast<float>(1u + tseg1) / static_cast<float>(tq_num);
    }
    else
    {
      fd_cfg.data_bitrate = 0;
      fd_cfg.data_sample_point = 0.0f;
    }

    return fdcans_[ch]->SetConfig(fd_cfg);
  }

  ErrorCode HandleMode(uint8_t ch, const GsUsb::DeviceMode &mode)
  {
    if (!host_format_ok_ || ch >= can_count_ || !cans_[ch]) return ErrorCode::ARG_ERR;

    switch (static_cast<GsUsb::CanMode>(mode.mode))
    {
      case GsUsb::CanMode::RESET:
        can_enabled_[ch] = false;
        fd_enabled_[ch] = false;
        break;

      case GsUsb::CanMode::START:
        can_enabled_[ch] = true;
        if (fd_supported_ && fdcans_[ch] && (mode.flags & GsUsb::GSCAN_MODE_FD))
        {
          fd_enabled_[ch] = true;
        }
        break;

      default:
        return ErrorCode::ARG_ERR;
    }

    auto &cfg = config_[ch];
    cfg.mode.loopback = (mode.flags & GsUsb::GSCAN_MODE_LOOP_BACK) != 0;
    cfg.mode.listen_only = (mode.flags & GsUsb::GSCAN_MODE_LISTEN_ONLY) != 0;
    cfg.mode.triple_sampling = (mode.flags & GsUsb::GSCAN_MODE_TRIPLE_SAMPLE) != 0;
    cfg.mode.one_shot = (mode.flags & GsUsb::GSCAN_MODE_ONE_SHOT) != 0;

    // HW_TIMESTAMP：按通道 enable，影响 Bulk IN 帧是否走 *_ts 变体
    timestamps_enabled_ch_[ch] = (mode.flags & GsUsb::GSCAN_MODE_HW_TIMESTAMP) != 0;

    // padding：不实现（host 若误传 flag，忽略）
    berr_enabled_[ch] = (mode.flags & GsUsb::GSCAN_MODE_BERR_REPORTING) != 0;

    const ErrorCode ec = cans_[ch]->SetConfig(cfg);

    if (fd_supported_ && fdcans_[ch])
    {
      auto &fd_cfg = fd_config_[ch];
      static_cast<CAN::Configuration &>(fd_cfg) = cfg;
      fd_cfg.fd_mode.fd_enabled = (mode.flags & GsUsb::GSCAN_MODE_FD) != 0;
      (void)fdcans_[ch]->SetConfig(fd_cfg);
    }

    return ec;
  }

  ErrorCode HandleBerr(uint8_t ch, uint32_t berr_on)
  {
    if (ch >= can_count_) return ErrorCode::ARG_ERR;
    berr_enabled_[ch] = (berr_on != 0);
    return ErrorCode::OK;
  }

  ErrorCode HandleIdentify(uint8_t, const GsUsb::Identify &id)
  {
    const bool on = (id.mode == static_cast<uint32_t>(GsUsb::IdentifyMode::ON));
    if (identify_gpio_) (void)identify_gpio_->Write(on);
    return ErrorCode::OK;
  }

  ErrorCode HandleSetTermination(uint8_t ch, const GsUsb::DeviceTerminationState &st)
  {
    if (ch >= can_count_) return ErrorCode::ARG_ERR;

    term_state_[ch] = static_cast<GsUsb::TerminationState>(
        st.state != 0 ? static_cast<uint32_t>(GsUsb::TerminationState::ON)
                      : static_cast<uint32_t>(GsUsb::TerminationState::OFF));

    if (termination_gpios_[ch])
    {
      const bool on = (term_state_[ch] == GsUsb::TerminationState::ON);
      (void)termination_gpios_[ch]->Write(on);
    }

    return ErrorCode::OK;
  }

  // ====== wire -> CAN pack ======
  static void HostWireToClassicPack(const WireHeader &wh, const uint8_t *payload,
                                    LibXR::CAN::ClassicPack &pack)
  {
    const uint32_t cid = wh.can_id;
    const bool is_eff = (cid & GsUsb::CAN_EFF_FLAG) != 0;
    const bool is_rtr = (cid & GsUsb::CAN_RTR_FLAG) != 0;

    if (is_eff)
    {
      pack.id = cid & GsUsb::CAN_EFF_MASK;
      pack.type = is_rtr ? LibXR::CAN::Type::REMOTE_EXTENDED : LibXR::CAN::Type::EXTENDED;
    }
    else
    {
      pack.id = cid & GsUsb::CAN_SFF_MASK;
      pack.type = is_rtr ? LibXR::CAN::Type::REMOTE_STANDARD : LibXR::CAN::Type::STANDARD;
    }

    uint8_t dlc = wh.can_dlc;
    if (dlc > 8u) dlc = 8u;
    pack.dlc = dlc;

    if (dlc > 0u) Memory::FastCopy(pack.data, payload, dlc);
  }

  static void HostWireToFdPack(const WireHeader &wh, const uint8_t *payload,
                               LibXR::FDCAN::FDPack &pack)
  {
    const uint32_t cid = wh.can_id;
    const bool is_eff = (cid & GsUsb::CAN_EFF_FLAG) != 0;
    const bool is_rtr = (cid & GsUsb::CAN_RTR_FLAG) != 0;

    if (is_eff)
    {
      pack.id = cid & GsUsb::CAN_EFF_MASK;
      pack.type = is_rtr ? LibXR::CAN::Type::REMOTE_EXTENDED : LibXR::CAN::Type::EXTENDED;
    }
    else
    {
      pack.id = cid & GsUsb::CAN_SFF_MASK;
      pack.type = is_rtr ? LibXR::CAN::Type::REMOTE_STANDARD : LibXR::CAN::Type::STANDARD;
    }

    uint8_t len = DlcToLen(wh.can_dlc);
    if (len > 64) len = 64;
    pack.len = len;

    if (len > 0u) Memory::FastCopy(pack.data, payload, len);
  }

  // ====== CAN pack -> QueueItem ======
  void ClassicPackToQueueItem(const LibXR::CAN::ClassicPack &pack, uint8_t ch,
                              QueueItem &qi)
  {
    uint32_t cid = 0;
    switch (pack.type)
    {
      case LibXR::CAN::Type::STANDARD:
        cid = (pack.id & GsUsb::CAN_SFF_MASK);
        break;
      case LibXR::CAN::Type::EXTENDED:
        cid = (pack.id & GsUsb::CAN_EFF_MASK) | GsUsb::CAN_EFF_FLAG;
        break;
      case LibXR::CAN::Type::REMOTE_STANDARD:
        cid = (pack.id & GsUsb::CAN_SFF_MASK) | GsUsb::CAN_RTR_FLAG;
        break;
      case LibXR::CAN::Type::REMOTE_EXTENDED:
        cid = (pack.id & GsUsb::CAN_EFF_MASK) | GsUsb::CAN_EFF_FLAG | GsUsb::CAN_RTR_FLAG;
        break;
      default:
        cid = pack.id & GsUsb::CAN_SFF_MASK;
        break;
    }

    qi.hdr.echo_id = ECHO_ID_RX;
    qi.hdr.can_id = cid;
    qi.hdr.can_dlc = (pack.dlc <= 8u) ? pack.dlc : 8u;
    qi.hdr.channel = ch;
    qi.hdr.flags = 0;
    qi.hdr.reserved = 0;

    qi.is_fd = false;
    qi.data_len = 8;
    qi.timestamp_us = MakeTimestampUs(ch);

    Memory::FastSet(qi.data.data(), 0, 8);
    if (qi.hdr.can_dlc > 0u) Memory::FastCopy(qi.data.data(), pack.data, qi.hdr.can_dlc);
  }

  void FdPackToQueueItem(const LibXR::FDCAN::FDPack &pack, uint8_t ch, QueueItem &qi)
  {
    uint32_t cid = 0;
    switch (pack.type)
    {
      case LibXR::CAN::Type::STANDARD:
        cid = (pack.id & GsUsb::CAN_SFF_MASK);
        break;
      case LibXR::CAN::Type::EXTENDED:
        cid = (pack.id & GsUsb::CAN_EFF_MASK) | GsUsb::CAN_EFF_FLAG;
        break;
      case LibXR::CAN::Type::REMOTE_STANDARD:
        cid = (pack.id & GsUsb::CAN_SFF_MASK) | GsUsb::CAN_RTR_FLAG;
        break;
      case LibXR::CAN::Type::REMOTE_EXTENDED:
        cid = (pack.id & GsUsb::CAN_EFF_MASK) | GsUsb::CAN_EFF_FLAG | GsUsb::CAN_RTR_FLAG;
        break;
      default:
        cid = pack.id & GsUsb::CAN_SFF_MASK;
        break;
    }

    qi.hdr.echo_id = ECHO_ID_RX;
    qi.hdr.can_id = cid;
    qi.hdr.can_dlc = LenToDlc(pack.len);
    qi.hdr.channel = ch;
    qi.hdr.flags = GsUsb::CAN_FLAG_FD;
    qi.hdr.reserved = 0;

    qi.is_fd = true;
    qi.data_len = 64;
    qi.timestamp_us = MakeTimestampUs(ch);

    Memory::FastSet(qi.data.data(), 0, 64);
    if (pack.len > 0u) Memory::FastCopy(qi.data.data(), pack.data, pack.len);
  }

  // ====== QueueItem -> wire bytes（严格符合 gs_host_frame + classic/canfd(+ts)）======
  std::size_t PackQueueItemToWire(const QueueItem &qi, uint8_t *out,
                                  std::size_t cap) const
  {
    if (cap < WIRE_HDR_SIZE) return 0;

    const uint8_t ch = qi.hdr.channel;
    const bool ts = (ch < can_count_) ? timestamps_enabled_ch_[ch] : false;
    const std::size_t payload = qi.is_fd ? WIRE_FD_DATA_SIZE : WIRE_CLASSIC_DATA_SIZE;
    const std::size_t total = WIRE_HDR_SIZE + payload + (ts ? WIRE_TS_SIZE : 0);

    if (total > cap) return 0;

    Memory::FastCopy(out, &qi.hdr, WIRE_HDR_SIZE);
    Memory::FastCopy(out + WIRE_HDR_SIZE, qi.data.data(), payload);

    if (ts)
    {
      // timestamp_us 紧跟 data 段：classic(8) / fd(64) 都符合 Linux 结构定义
      Memory::FastCopy(out + WIRE_HDR_SIZE + payload, &qi.timestamp_us, WIRE_TS_SIZE);
    }

    return total;
  }

  bool ErrorPackToHostErrorFrame(uint8_t ch, const LibXR::CAN::ClassicPack &err_pack,
                                 QueueItem &qi)
  {
    if (ch >= can_count_ || !cans_[ch]) return false;
    if (!berr_enabled_[ch]) return false;
    if (!LibXR::CAN::IsErrorId(err_pack.id)) return false;

    // Linux can/error.h 对齐（同你之前版本）
    constexpr uint8_t LNX_CAN_ERR_CRTL_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_CRTL_RX_WARNING = 0x04;
    constexpr uint8_t LNX_CAN_ERR_CRTL_TX_WARNING = 0x08;
    constexpr uint8_t LNX_CAN_ERR_CRTL_RX_PASSIVE = 0x10;
    constexpr uint8_t LNX_CAN_ERR_CRTL_TX_PASSIVE = 0x20;

    constexpr uint8_t LNX_CAN_ERR_PROT_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_PROT_FORM = 0x02;
    constexpr uint8_t LNX_CAN_ERR_PROT_STUFF = 0x04;
    constexpr uint8_t LNX_CAN_ERR_PROT_BIT0 = 0x08;
    constexpr uint8_t LNX_CAN_ERR_PROT_BIT1 = 0x10;
    constexpr uint8_t LNX_CAN_ERR_PROT_TX = 0x80;

    constexpr uint8_t LNX_CAN_ERR_PROT_LOC_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_PROT_LOC_ACK = 0x19;

    constexpr uint32_t LNX_CAN_ERR_CNT = 0x00000200U;

    bool ec_valid = false;
    uint32_t txerr = 0, rxerr = 0;
    {
      LibXR::CAN::ErrorState es{};
      if (cans_[ch]->GetErrorState(es) == ErrorCode::OK)
      {
        ec_valid = true;
        txerr = es.tx_error_counter;
        rxerr = es.rx_error_counter;
      }
    }

    const uint8_t txerr_u8 = (txerr > 255U) ? 255U : static_cast<uint8_t>(txerr);
    const uint8_t rxerr_u8 = (rxerr > 255U) ? 255U : static_cast<uint8_t>(rxerr);

    const auto eid = LibXR::CAN::ToErrorID(err_pack.id);

    uint32_t cid = GsUsb::CAN_ERR_FLAG;
    std::array<uint8_t, 8> d{};
    Memory::FastSet(d.data(), 0, 8);

    switch (eid)
    {
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BUS_OFF:
        cid |= GsUsb::CAN_ERR_BUSOFF;
        break;

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_WARNING:
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE:
      {
        cid |= GsUsb::CAN_ERR_CRTL;
        uint8_t ctrl = LNX_CAN_ERR_CRTL_UNSPEC;

        if (ec_valid)
        {
          if (eid == LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE)
          {
            if (txerr >= 128U) ctrl |= LNX_CAN_ERR_CRTL_TX_PASSIVE;
            if (rxerr >= 128U) ctrl |= LNX_CAN_ERR_CRTL_RX_PASSIVE;
          }
          else
          {
            if (txerr >= 96U) ctrl |= LNX_CAN_ERR_CRTL_TX_WARNING;
            if (rxerr >= 96U) ctrl |= LNX_CAN_ERR_CRTL_RX_WARNING;
          }
        }

        if (ctrl == LNX_CAN_ERR_CRTL_UNSPEC)
        {
          ctrl = (eid == LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE)
                     ? static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_PASSIVE |
                                            LNX_CAN_ERR_CRTL_RX_PASSIVE)
                     : static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_WARNING |
                                            LNX_CAN_ERR_CRTL_RX_WARNING);
        }

        d[1] = ctrl;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ACK:
        cid |= GsUsb::CAN_ERR_ACK;
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_UNSPEC | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_ACK;
        break;

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_STUFF:
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_STUFF | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_FORM:
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_FORM | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BIT0:
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_BIT0 | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BIT1:
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_BIT1 | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;

      default:
        cid |= GsUsb::CAN_ERR_PROT;
        d[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_UNSPEC | LNX_CAN_ERR_PROT_TX);
        d[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
    }

    cid |= LNX_CAN_ERR_CNT;
    d[6] = txerr_u8;
    d[7] = rxerr_u8;

    qi.hdr.echo_id = ECHO_ID_RX;
    qi.hdr.can_id = cid;
    qi.hdr.can_dlc = GsUsb::CAN_ERR_DLC;
    qi.hdr.channel = ch;
    qi.hdr.flags = 0;
    qi.hdr.reserved = 0;

    qi.is_fd = false;
    qi.data_len = 8;
    qi.timestamp_us = MakeTimestampUs(ch);

    Memory::FastCopy(qi.data.data(), d.data(), 8);

    return true;
  }

  // ================= 工具函数 =================
  uint32_t MakeTimestampUs(uint8_t ch) const
  {
    if (ch < can_count_ && timestamps_enabled_ch_[ch] &&
        LibXR::Timebase::timebase != nullptr)
    {
      return static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
    }
    return 0u;
  }

  // Linux gs_usb 的 BREQ_TIMESTAMP 读取设备级计数器：不应依赖通道是否启用
  uint32_t MakeTimestampUsGlobal() const
  {
    if (LibXR::Timebase::timebase != nullptr)
    {
      return static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
    }
    return 0u;
  }

  static uint8_t DlcToLen(uint8_t dlc)
  {
    static constexpr uint8_t table[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                          8, 12, 16, 20, 24, 32, 48, 64};
    return (dlc < 16) ? table[dlc] : 64;
  }

  static uint8_t LenToDlc(uint8_t len)
  {
    if (len <= 8) return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
  }

  // ============ USER_ID 的 RAM/Database 存取封装 ============
  uint32_t GetUserIdFromStorage() const
  {
    if (database_ == nullptr) return user_id_ram_;

    LibXR::Database::Key<uint32_t> key(*database_, "user_id", user_id_ram_);
    return static_cast<uint32_t>(key);
  }

  void SetUserIdToStorage(uint32_t value)
  {
    user_id_ram_ = value;

    if (database_ == nullptr) return;

    LibXR::Database::Key<uint32_t> key(*database_, "user_id", 0u);
    (void)key.Set(value);
  }
};

}  // namespace LibXR::USB
