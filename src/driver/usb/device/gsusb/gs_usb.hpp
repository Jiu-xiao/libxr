#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>

#include "can.hpp"  // 提供 LibXR::CAN / LibXR::FDCAN 抽象
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
 * @tparam CanChNum   编译期固定的 CAN 通道数（写多少就是多少）
 * @tparam DatabaseType 可选的数据库类型（例如 LibXR::DatabaseRaw<4>），默认为 void
 * 表示不启用数据库
 */
template <std::size_t CanChNum, typename DatabaseType = void>
class GsUsbClass : public DeviceClass
{
  static_assert(CanChNum > 0 && CanChNum <= 255, "CanChNum must be in (0, 255]");
  static constexpr uint8_t CAN_CH_NUM = static_cast<uint8_t>(CanChNum);

 public:
  // ============== 构造：经典 CAN ==============
  GsUsbClass(std::initializer_list<LibXR::CAN *> cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP1,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP2,
             LibXR::GPIO *identify_gpio = nullptr,
             LibXR::GPIO *termination_gpio = nullptr, DatabaseType *database = nullptr)
      : data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        identify_gpio_(identify_gpio),
        termination_gpio_(termination_gpio),
        database_(database)
  {
    ASSERT(cans.size() == CAN_CH_NUM);
    std::size_t i = 0;
    for (auto *p : cans)
    {
      cans_[i++] = p;
    }

    InitDeviceConfigClassic();
  }

  // ============== 构造：FDCAN ==============
  GsUsbClass(std::initializer_list<LibXR::FDCAN *> fd_cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
             LibXR::GPIO *identify_gpio = nullptr,
             LibXR::GPIO *termination_gpio = nullptr, DatabaseType *database = nullptr)
      : fd_supported_(true),
        data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        identify_gpio_(identify_gpio),
        termination_gpio_(termination_gpio),
        database_(database)
  {
    ASSERT(fd_cans.size() == CAN_CH_NUM);
    std::size_t i = 0;
    for (auto *p : fd_cans)
    {
      fdcans_[i] = p;
      cans_[i] = p;  // 向上转成 CAN*
      ++i;
    }

    InitDeviceConfigFd();
  }

  /// 当前 Host 是否已完成 endian 握手（HOST_FORMAT）
  bool IsHostFormatOK() const { return host_format_ok_; }

 protected:
  // ================= ConfigDescriptorItem 覆盖 =================

  void Init(EndpointPool &endpoint_pool, uint8_t start_itf_num) override
  {
    inited_ = false;
    interface_num_ = start_itf_num;

    auto ans = endpoint_pool.Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
    ASSERT(ans == ErrorCode::OK);

    ans = endpoint_pool.Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
    ASSERT(ans == ErrorCode::OK);

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
    timestamps_enabled_ = false;
    pad_pkts_to_max_pkt_size_ = false;

    for (uint8_t i = 0; i < can_count_; ++i)
    {
      can_enabled_[i] = false;
      berr_enabled_[i] = false;
      fd_enabled_[i] = false;
      term_state_[i] = GsUsb::TerminationState::OFF;
    }

    // 注册 CAN RX 回调
    if (!can_rx_registered_)
    {
      for (uint8_t ch = 0; ch < can_count_; ++ch)
      {
        if (!cans_[ch])
        {
          continue;
        }

        can_rx_ctx_[ch].self = this;
        can_rx_ctx_[ch].ch = ch;
        can_rx_cb_[ch] = LibXR::CAN::Callback::Create(OnCanRxStatic, &can_rx_ctx_[ch]);

        // 所有经典帧 + ERROR 帧都订阅
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
        if (!fdcans_[ch])
        {
          continue;
        }

        fd_can_rx_ctx_[ch].self = this;
        fd_can_rx_ctx_[ch].ch = ch;
        fd_can_rx_cb_[ch] =
            LibXR::FDCAN::CallbackFD::Create(OnFdCanRxStatic, &fd_can_rx_ctx_[ch]);

        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::STANDARD);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::EXTENDED);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_STANDARD);
        fdcans_[ch]->Register(fd_can_rx_cb_[ch], LibXR::CAN::Type::REMOTE_EXTENDED);
        // ERROR 帧仍然通过 CAN::Register 分发，这里不用再注册
      }
      fd_can_rx_registered_ = true;
    }

    inited_ = true;

    // 初始允许 host 发送 OUT：根据队列空余情况尝试 Arm 一次
    MaybeArmOutTransfer();
  }

  void Deinit(EndpointPool &endpoint_pool) override
  {
    inited_ = false;
    host_format_ok_ = false;
    timestamps_enabled_ = false;
    pad_pkts_to_max_pkt_size_ = false;

    for (uint8_t i = 0; i < can_count_; ++i)
    {
      can_enabled_[i] = false;
      berr_enabled_[i] = false;
      fd_enabled_[i] = false;
      term_state_[i] = GsUsb::TerminationState::OFF;
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
    if (!inited_)
    {
      return false;
    }

    return (ep_data_in_ && ep_data_in_->GetAddress() == ep_addr) ||
           (ep_data_out_ && ep_data_out_->GetAddress() == ep_addr);
  }

  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }

  // ================= 控制请求处理 =================

  /// 不使用 Class 请求，全部走 Vendor 请求
  ErrorCode OnClassRequest(bool, uint8_t, uint16_t, uint16_t, uint16_t,
                           DeviceClass::RequestResult &) override
  {
    return ErrorCode::NOT_SUPPORT;
  }

  /// Vendor Request 的 SETUP 阶段
  ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                            uint16_t wLength, uint16_t wIndex,
                            DeviceClass::RequestResult &result) override
  {
    UNUSED(in_isr);
    UNUSED(wIndex);  // interface number，一般只有 0

    auto req = static_cast<GsUsb::BReq>(bRequest);

    switch (req)
    {
      // ===== Device -> Host =====
      case GsUsb::BReq::BT_CONST:
      {
        if (wLength < sizeof(bt_const_))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data = ConstRawData{reinterpret_cast<const uint8_t *>(&bt_const_),
                                         sizeof(bt_const_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BT_CONST_EXT:
      {
        if (!fd_supported_)
        {
          return ErrorCode::NOT_SUPPORT;
        }

        if (wLength < sizeof(bt_const_ext_))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data = ConstRawData{
            reinterpret_cast<const uint8_t *>(&bt_const_ext_), sizeof(bt_const_ext_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::DEVICE_CONFIG:
      {
        if (wLength < sizeof(dev_cfg_))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&dev_cfg_), sizeof(dev_cfg_)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::TIMESTAMP:
      {
        ctrl_buf_.timestamp_us = MakeTimestampUs();

        if (wLength < sizeof(ctrl_buf_.timestamp_us))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.timestamp_us),
                         sizeof(ctrl_buf_.timestamp_us)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_TERMINATION:
      {
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_buf_.term.state = static_cast<uint32_t>(term_state_[wValue]);

        if (wLength < sizeof(ctrl_buf_.term))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data = ConstRawData{
            reinterpret_cast<const uint8_t *>(&ctrl_buf_.term), sizeof(ctrl_buf_.term)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_STATE:
      {
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        // 使用底层 CAN::GetErrorState，如果没实现则回退到默认
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
            {
              st = GsUsb::CanState::BUS_OFF;
            }
            else if (es.error_passive)
            {
              st = GsUsb::CanState::ERROR_PASSIVE;
            }
            else if (es.error_warning)
            {
              st = GsUsb::CanState::ERROR_WARNING;
            }
            else
            {
              st = GsUsb::CanState::ERROR_ACTIVE;
            }

            rxerr = es.rx_error_counter;
            txerr = es.tx_error_counter;
          }
        }

        ctrl_buf_.dev_state.state = static_cast<uint32_t>(st);
        ctrl_buf_.dev_state.rxerr = rxerr;
        ctrl_buf_.dev_state.txerr = txerr;

        if (wLength < sizeof(ctrl_buf_.dev_state))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.dev_state),
                         sizeof(ctrl_buf_.dev_state)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::GET_USER_ID:
      {
        ctrl_buf_.user_id = GetUserIdFromStorage();

        if (wLength < sizeof(ctrl_buf_.user_id))
        {
          return ErrorCode::ARG_ERR;
        }

        result.write_data =
            ConstRawData{reinterpret_cast<const uint8_t *>(&ctrl_buf_.user_id),
                         sizeof(ctrl_buf_.user_id)};
        return ErrorCode::OK;
      }

        // ===== Host -> Device（有 DATA 阶段） =====

      case GsUsb::BReq::HOST_FORMAT:
      {
        if (wLength != sizeof(GsUsb::HostConfig))
        {
          return ErrorCode::ARG_ERR;
        }

        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.host_cfg),
                                   sizeof(GsUsb::HostConfig)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BITTIMING:
      {
        if (wLength != sizeof(GsUsb::DeviceBitTiming))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.bt),
                                   sizeof(GsUsb::DeviceBitTiming)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::DATA_BITTIMING:
      {
        if (!fd_supported_)
        {
          return ErrorCode::NOT_SUPPORT;
        }

        if (wLength != sizeof(GsUsb::DeviceBitTiming))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.bt),
                                   sizeof(GsUsb::DeviceBitTiming)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::MODE:
      {
        if (wLength != sizeof(GsUsb::DeviceMode))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.mode),
                                   sizeof(GsUsb::DeviceMode)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::BERR:
      {
        if (wLength != sizeof(uint32_t))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data =
            RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.berr_on), sizeof(uint32_t)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::IDENTIFY:
      {
        if (wLength != sizeof(GsUsb::Identify))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.identify),
                                   sizeof(GsUsb::Identify)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::SET_TERMINATION:
      {
        if (wLength != sizeof(GsUsb::DeviceTerminationState))
        {
          return ErrorCode::ARG_ERR;
        }
        if (wValue >= can_count_)
        {
          return ErrorCode::ARG_ERR;
        }

        ctrl_target_channel_ = static_cast<uint8_t>(wValue);
        result.read_data = RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.term),
                                   sizeof(GsUsb::DeviceTerminationState)};
        return ErrorCode::OK;
      }

      case GsUsb::BReq::SET_USER_ID:
      {
        if (wLength != sizeof(uint32_t))
        {
          return ErrorCode::ARG_ERR;
        }
        result.read_data =
            RawData{reinterpret_cast<uint8_t *>(&ctrl_buf_.user_id), sizeof(uint32_t)};
        return ErrorCode::OK;
      }

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  /// 控制传输 DATA 阶段（Class + Vendor 共用），这里用 bRequest 区分
  ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData &data) override
  {
    UNUSED(in_isr);

    auto req = static_cast<GsUsb::BReq>(bRequest);

    switch (req)
    {
      case GsUsb::BReq::HOST_FORMAT:
        if (data.size_ != sizeof(GsUsb::HostConfig))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleHostFormat(ctrl_buf_.host_cfg);

      case GsUsb::BReq::BITTIMING:
        if (data.size_ != sizeof(GsUsb::DeviceBitTiming))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleBitTiming(ctrl_target_channel_, ctrl_buf_.bt);

      case GsUsb::BReq::DATA_BITTIMING:
        if (!fd_supported_)
        {
          return ErrorCode::NOT_SUPPORT;
        }
        if (data.size_ != sizeof(GsUsb::DeviceBitTiming))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleDataBitTiming(ctrl_target_channel_, ctrl_buf_.bt);

      case GsUsb::BReq::MODE:
        if (data.size_ != sizeof(GsUsb::DeviceMode))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleMode(ctrl_target_channel_, ctrl_buf_.mode);

      case GsUsb::BReq::BERR:
        if (data.size_ != sizeof(uint32_t))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleBerr(ctrl_target_channel_, ctrl_buf_.berr_on);

      case GsUsb::BReq::IDENTIFY:
        if (data.size_ != sizeof(GsUsb::Identify))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleIdentify(ctrl_target_channel_, ctrl_buf_.identify);

      case GsUsb::BReq::SET_TERMINATION:
        if (data.size_ != sizeof(GsUsb::DeviceTerminationState))
        {
          return ErrorCode::ARG_ERR;
        }
        return HandleSetTermination(ctrl_target_channel_, ctrl_buf_.term);

      case GsUsb::BReq::SET_USER_ID:
        if (data.size_ != sizeof(uint32_t))
        {
          return ErrorCode::ARG_ERR;
        }
        SetUserIdToStorage(ctrl_buf_.user_id);
        return ErrorCode::OK;

      case GsUsb::BReq::DEVICE_CONFIG:
      case GsUsb::BReq::BT_CONST:
      case GsUsb::BReq::BT_CONST_EXT:
      case GsUsb::BReq::TIMESTAMP:
      case GsUsb::BReq::GET_TERMINATION:
      case GsUsb::BReq::GET_STATE:
      case GsUsb::BReq::GET_USER_ID:
        // 这些请求在 SETUP 阶段就已经完成，不会有 DATA OUT
        return ErrorCode::OK;

      default:
        return ErrorCode::NOT_SUPPORT;
    }
  }

  // ================= Bulk 端点回调 =================

  static void OnDataOutCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataOutComplete(in_isr, data);
  }

  static void OnDataInCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataInComplete(in_isr, data);
  }

  void OnDataOutComplete(bool in_isr, ConstRawData &data)
  {
    UNUSED(in_isr);

    if (!ep_data_out_)
    {
      return;
    }

    const std::size_t rxlen = data.size_;
    if (rxlen < GsUsb::HOST_FRAME_CLASSIC_SIZE)
    {
      // 长度不够一个基本 classic 帧，忽略并根据队列情况尝试重新 Arm OUT
      MaybeArmOutTransfer();
      return;
    }

    const auto *hf = reinterpret_cast<const GsUsb::HostFrame *>(data.addr_);

    const uint8_t ch = hf->channel;
    if (ch >= can_count_ || !cans_[ch])
    {
      MaybeArmOutTransfer();
      return;
    }

    const bool is_fd = (hf->flags & GsUsb::CAN_FLAG_FD) != 0;

    if (is_fd)
    {
      if (!fd_supported_ || !fdcans_[ch] || !fd_enabled_[ch])
      {
        MaybeArmOutTransfer();
        return;
      }

      if (rxlen < GsUsb::HOST_FRAME_FD_SIZE)
      {
        MaybeArmOutTransfer();
        return;
      }

      LibXR::FDCAN::FDPack pack{};
      HostFrameToFdPack(*hf, pack);
      (void)fdcans_[ch]->AddMessage(pack);
    }
    else
    {
      if (!can_enabled_[ch])
      {
        MaybeArmOutTransfer();
        return;
      }

      // Classic CAN：注意 HostFrameToClassicPack 会忽略 CAN_ERR_FLAG，
      // 所以 host 不会通过 OUT 注入错误帧到总线
      LibXR::CAN::ClassicPack pack{};
      HostFrameToClassicPack(*hf, pack);
      (void)cans_[ch]->AddMessage(pack);
    }

    // TX echo：Host 通过 echo_id 跟踪 TX buffer，需要设备回送才能释放
    if (hf->echo_id != GsUsb::ECHO_ID_INVALID)
    {
      GsUsb::HostFrame echo = *hf;
      echo.timestamp_us = MakeTimestampUs();
      EnqueueHostFrame(echo, in_isr);
    }

    // 处理本帧完成后，再根据队列情况尝试 Arm 下一轮 OUT
    MaybeArmOutTransfer();
  }

  void OnDataInComplete(bool in_isr, ConstRawData &data)
  {
    UNUSED(in_isr);
    UNUSED(data);

    // 一个 IN 传输结束，EP 应该变回 IDLE
    // 看队列里还有没有帧，有的话继续发
    TryKickTx(false);
  }

 private:
  // 队列长度（HostFrame 个数）
  static constexpr size_t RX_QUEUE_SIZE = 64;
  static constexpr size_t ECHO_QUEUE_SIZE = 64;

  // ================= 成员 =================

  // 通道数组
  std::array<LibXR::CAN *, CanChNum> cans_{};      // nullptr 填充
  std::array<LibXR::FDCAN *, CanChNum> fdcans_{};  // nullptr 填充
  bool fd_supported_ = false;

  uint8_t can_count_ = CAN_CH_NUM;

  Endpoint::EPNumber data_in_ep_num_;
  Endpoint::EPNumber data_out_ep_num_;

  Endpoint *ep_data_in_ = nullptr;
  Endpoint *ep_data_out_ = nullptr;

  bool inited_ = false;
  uint8_t interface_num_ = 0;

  LibXR::GPIO *identify_gpio_ = nullptr;     // Identify LED（可空）
  LibXR::GPIO *termination_gpio_ = nullptr;  // 终端电阻控制 GPIO（可空，全局）

  // 可选数据库指针：nullptr 表示禁用数据库
  DatabaseType *database_ = nullptr;
  uint32_t user_id_ram_ = 0;  // 即便有数据库，也保留一份 RAM 缓存

  // EP 回调
  LibXR::Callback<LibXR::ConstRawData &> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData &> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataInCompleteStatic, this);

  // GsUsb 协议相关状态
  GsUsb::DeviceConfig dev_cfg_{};
  GsUsb::DeviceBTConst bt_const_{};
  GsUsb::DeviceBTConstExtended bt_const_ext_{};  // FD 能力

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

  // 每个通道一份 CAN 配置
  std::array<LibXR::CAN::Configuration, CanChNum> config_{};

  // FD 模式下，每个通道一份 FDCAN 配置
  std::array<LibXR::FDCAN::Configuration, CanChNum> fd_config_{};

  bool host_format_ok_ = false;
  bool can_enabled_[CanChNum] = {false};
  bool berr_enabled_[CanChNum] = {false};
  bool fd_enabled_[CanChNum] = {false};
  bool timestamps_enabled_ = false;
  bool pad_pkts_to_max_pkt_size_ = false;

  GsUsb::TerminationState term_state_[CanChNum] = {
      GsUsb::TerminationState::OFF,
  };

  // 当前正在处理控制请求的目标通道（由 wValue 决定）
  uint8_t ctrl_target_channel_ = 0;

#pragma pack(push, 1)
  /// GsUsb 配置描述符块：1 个接口 + 2 个 BULK 端点
  struct GsUsbDescBlock
  {
    InterfaceDescriptor intf;
    EndpointDescriptor ep_out;
    EndpointDescriptor ep_in;
  } desc_block_{};
#pragma pack(pop)

  // OUT 接收缓冲区：最大一帧（FD + timestamp）
  uint8_t rx_buf_[GsUsb::HOST_FRAME_FD_TS_SIZE]{};

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

  // 静态 trampoline
  static void OnCanRxStatic(bool in_isr, CanRxCtx *ctx,
                            const LibXR::CAN::ClassicPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_)
    {
      return;
    }
    ctx->self->OnCanRx(in_isr, ctx->ch, pack);
  }

  static void OnFdCanRxStatic(bool in_isr, FdCanRxCtx *ctx,
                              const LibXR::FDCAN::FDPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_)
    {
      return;
    }
    ctx->self->OnFdCanRx(in_isr, ctx->ch, pack);
  }

  // 真正的 CAN RX 处理函数
  void OnCanRx(bool in_isr, uint8_t ch, const LibXR::CAN::ClassicPack &pack)
  {
    UNUSED(in_isr);

    if (ch >= can_count_ || !ep_data_in_)
    {
      return;
    }

    if (pack.type == LibXR::CAN::Type::ERROR)
    {
      // 错误帧 → Linux CAN_ERR_* HostFrame（仅在 berr_enabled_ 时）
      GsUsb::HostFrame hf{};
      if (ErrorPackToHostErrorFrame(ch, pack, hf))
      {
        EnqueueHostFrame(hf, in_isr);
      }
      return;
    }

    if (!can_enabled_[ch])
    {
      return;
    }

    GsUsb::HostFrame hf{};
    ClassicPackToHostFrame(pack, hf);
    hf.channel = ch;

    EnqueueHostFrame(hf, in_isr);
  }

  void OnFdCanRx(bool in_isr, uint8_t ch, const LibXR::FDCAN::FDPack &pack)
  {
    UNUSED(in_isr);

    if (!fd_supported_ || ch >= can_count_ || !fd_enabled_[ch] || !ep_data_in_)
    {
      return;
    }

    GsUsb::HostFrame hf{};
    FdPackToHostFrame(pack, hf);
    hf.channel = ch;

    // BRS / ESI 按 FD 配置粗略设置（没有 per-frame 信息）
    const auto &fd_cfg = fd_config_[ch];
    if (fd_cfg.fd_mode.brs)
    {
      hf.flags |= GsUsb::CAN_FLAG_BRS;
    }
    if (fd_cfg.fd_mode.esi)
    {
      hf.flags |= GsUsb::CAN_FLAG_ESI;
    }

    EnqueueHostFrame(hf, in_isr);
  }

  // ================= TX 队列：CAN RX / echo 各一个 =================

  // CAN RX 来的帧（普通数据帧 + 错误帧）
  LibXR::LockFreeQueue<GsUsb::HostFrame> rx_queue_{RX_QUEUE_SIZE};
  // TX echo 帧（host 发出的带 echo_id 的帧）
  LibXR::LockFreeQueue<GsUsb::HostFrame> echo_queue_{ECHO_QUEUE_SIZE};

  /// 入队一个要发给 Host 的 HostFrame（CAN RX / 错误帧 / TX echo 共用）
  bool EnqueueHostFrame(const GsUsb::HostFrame &hf, bool in_isr)
  {
    UNUSED(in_isr);

    const bool is_echo = (hf.echo_id != GsUsb::ECHO_ID_INVALID);

    ErrorCode ec = is_echo ? echo_queue_.Push(hf) : rx_queue_.Push(hf);
    if (ec != ErrorCode::OK && is_echo)
    {
      // 队列满，丢帧；这里可以加统计计数
      ASSERT(false);
      return false;
    }

    // 新帧入队后，尝试启动 IN 方向发送
    TryKickTx(in_isr);

    // 入队后检查队列是否仍允许 host 继续发 OUT
    MaybeArmOutTransfer();

    return true;
  }

  /// 尝试启动下一次 BULK IN 传输（从 echo_queue_ 优先，其次 rx_queue_）
  void TryKickTx(bool in_isr)
  {
    UNUSED(in_isr);

    if (!ep_data_in_)
    {
      return;
    }

    // 利用 EP 自己的状态机：不是 IDLE 就不发
    if (ep_data_in_->GetState() != Endpoint::State::IDLE)
    {
      return;
    }

    GsUsb::HostFrame hf{};

    // 优先发送 echo 帧，保证 host TX 的 ACK 不会被饿死
    ErrorCode ec = echo_queue_.Pop(hf);
    if (ec != ErrorCode::OK)
    {
      ec = rx_queue_.Pop(hf);
      if (ec != ErrorCode::OK)
      {
        // 没有待发帧
        return;
      }
    }

    auto buffer = ep_data_in_->GetBuffer();

    // 计算帧长度
    std::size_t len = GetHostFrameSize(hf);

    // padding 模式：仅对非 FD 帧用 MaxPacketSize 对齐（与参考实现一致）
    uint8_t tmp_buf[GsUsb::HOST_FRAME_FD_TS_SIZE];  // 单帧最大
    const uint16_t mps = ep_data_in_->MaxPacketSize();
    ASSERT(mps <= sizeof(tmp_buf));

    const bool is_fd = (hf.flags & GsUsb::CAN_FLAG_FD) != 0;
    uint8_t *send_ptr = reinterpret_cast<uint8_t *>(&hf);
    uint16_t send_len = static_cast<uint16_t>(len);

    if (pad_pkts_to_max_pkt_size_ && !is_fd && len < mps)
    {
      Memory::FastCopy(tmp_buf, &hf, len);
      Memory::FastSet(tmp_buf + len, 0, mps - len);
      send_ptr = tmp_buf;
      send_len = mps;
    }

    Memory::FastCopy(buffer.addr_, send_ptr, send_len);
    ep_data_in_->Transfer(send_len);

    // 出队后空余增大，有机会恢复 host 的 OUT 传输
    MaybeArmOutTransfer();
  }

  /// 根据队列空间情况，尝试启动下一次 BULK OUT（host->device）接收
  /// 满足：两个队列都有空余 && 当前 OUT EP 处于 IDLE
  void MaybeArmOutTransfer()
  {
    if (!ep_data_out_)
    {
      return;
    }

    // 端点不空闲就不 Arm
    if (ep_data_out_->GetState() != Endpoint::State::IDLE)
    {
      return;
    }

    // 任意一个队列满，都不要继续接受 host 发来的新帧
    const bool rx_has_space = (rx_queue_.EmptySize() > 0);
    const bool echo_has_space = (echo_queue_.EmptySize() > 0);

    if (!rx_has_space || !echo_has_space)
    {
      // 至少有一个队列已满，禁用新的 OUT Arm
      return;
    }

    // 预留最大一帧（FD + timestamp）
    RawData rx_raw{rx_buf_, static_cast<size_t>(GsUsb::HOST_FRAME_FD_TS_SIZE)};
    ep_data_out_->TransferMultiBulk(rx_raw);
  }

  // ================= 业务处理函数 =================

  void InitDeviceConfigClassic()
  {
    can_count_ = CAN_CH_NUM;
    ASSERT(can_count_ > 0);
    ASSERT(cans_[0] != nullptr);

    // DeviceConfig
    dev_cfg_.reserved1 = 0;
    dev_cfg_.reserved2 = 0;
    dev_cfg_.reserved3 = 0;
    dev_cfg_.icount = static_cast<uint8_t>(can_count_ - 1);
    dev_cfg_.sw_version = 2;
    dev_cfg_.hw_version = 1;

    // Nominal BT const（用 ch0 的时钟）
    const uint32_t fclk = cans_[0]->GetClockFreq();
    bt_const_.feature = GsUsb::CAN_FEAT_LISTEN_ONLY | GsUsb::CAN_FEAT_LOOP_BACK |
                        GsUsb::CAN_FEAT_TRIPLE_SAMPLE | GsUsb::CAN_FEAT_ONE_SHOT |
                        GsUsb::CAN_FEAT_HW_TIMESTAMP | GsUsb::CAN_FEAT_IDENTIFY |
                        GsUsb::CAN_FEAT_PAD_PKTS_TO_MAX_PKT_SIZE |
                        GsUsb::CAN_FEAT_BERR_REPORTING;
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

    // DeviceConfig
    dev_cfg_.reserved1 = 0;
    dev_cfg_.reserved2 = 0;
    dev_cfg_.reserved3 = 0;
    dev_cfg_.icount = static_cast<uint8_t>(can_count_ - 1);
    dev_cfg_.sw_version = 2;
    dev_cfg_.hw_version = 1;

    const uint32_t fclk = cans_[0]->GetClockFreq();
    bt_const_.feature = GsUsb::CAN_FEAT_LISTEN_ONLY | GsUsb::CAN_FEAT_LOOP_BACK |
                        GsUsb::CAN_FEAT_TRIPLE_SAMPLE | GsUsb::CAN_FEAT_ONE_SHOT |
                        GsUsb::CAN_FEAT_HW_TIMESTAMP | GsUsb::CAN_FEAT_IDENTIFY |
                        GsUsb::CAN_FEAT_PAD_PKTS_TO_MAX_PKT_SIZE |
                        GsUsb::CAN_FEAT_BERR_REPORTING | GsUsb::CAN_FEAT_FD |
                        GsUsb::CAN_FEAT_BT_CONST_EXT | GsUsb::CAN_FEAT_TERMINATION;
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
    host_format_ok_ = (cfg.byte_order == 0x0000beefu);
    return ErrorCode::OK;
  }

  ErrorCode HandleBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt)
  {
    if (!host_format_ok_)
    {
      return ErrorCode::ARG_ERR;
    }
    if (ch >= can_count_ || !cans_[ch])
    {
      return ErrorCode::ARG_ERR;
    }
    if (bt.brp == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
    const uint32_t tseg2 = bt.phase_seg2;
    const uint32_t tq_num = 1u + tseg1 + tseg2;
    if (tq_num == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    const uint32_t fclk = cans_[ch]->GetClockFreq();

    auto &cfg = config_[ch];
    cfg.bit_timing.brp = bt.brp;
    cfg.bit_timing.prop_seg = bt.prop_seg;
    cfg.bit_timing.phase_seg1 = bt.phase_seg1;
    cfg.bit_timing.phase_seg2 = bt.phase_seg2;
    cfg.bit_timing.sjw = bt.sjw;

    cfg.bitrate = fclk / (bt.brp * tq_num);
    cfg.sample_point = static_cast<float>(1u + tseg1) / static_cast<float>(tq_num);

    // FD 模式下，同步 nominal 部分到 fd_config_
    if (fd_supported_ && fdcans_[ch])
    {
      auto &fd_cfg = fd_config_[ch];
      static_cast<CAN::Configuration &>(fd_cfg) = cfg;
      // data 部分在 DATA_BITTIMING 里配置
    }

    return cans_[ch]->SetConfig(cfg);
  }

  ErrorCode HandleDataBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt)
  {
    if (!host_format_ok_)
    {
      return ErrorCode::ARG_ERR;
    }
    if (!fd_supported_ || ch >= can_count_ || !fdcans_[ch])
    {
      return ErrorCode::NOT_SUPPORT;
    }
    if (bt.brp == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
    const uint32_t tseg2 = bt.phase_seg2;
    const uint32_t tq_num = 1u + tseg1 + tseg2;
    if (tq_num == 0)
    {
      return ErrorCode::ARG_ERR;
    }

    const uint32_t fclk = fdcans_[ch]->GetClockFreq();

    auto &fd_cfg = fd_config_[ch];
    // 确保 nominal 部分有值：如果没配过，就使用 config_[ch]
    static_cast<CAN::Configuration &>(fd_cfg) = config_[ch];

    fd_cfg.data_timing.brp = bt.brp;
    fd_cfg.data_timing.prop_seg = bt.prop_seg;
    fd_cfg.data_timing.phase_seg1 = bt.phase_seg1;
    fd_cfg.data_timing.phase_seg2 = bt.phase_seg2;
    fd_cfg.data_timing.sjw = bt.sjw;

    fd_cfg.data_bitrate = fclk / (bt.brp * tq_num);
    fd_cfg.data_sample_point =
        static_cast<float>(1u + tseg1) / static_cast<float>(tq_num);

    return fdcans_[ch]->SetConfig(fd_cfg);
  }

  ErrorCode HandleMode(uint8_t ch, const GsUsb::DeviceMode &mode)
  {
    if (!host_format_ok_)
    {
      return ErrorCode::ARG_ERR;
    }
    if (ch >= can_count_ || !cans_[ch])
    {
      return ErrorCode::ARG_ERR;
    }

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

    timestamps_enabled_ = (mode.flags & GsUsb::GSCAN_MODE_HW_TIMESTAMP) != 0;
    pad_pkts_to_max_pkt_size_ =
        (mode.flags & GsUsb::GSCAN_MODE_PAD_PKTS_TO_MAX_PKT_SIZE) != 0;

    berr_enabled_[ch] = (mode.flags & GsUsb::GSCAN_MODE_BERR_REPORTING) != 0;

    ErrorCode ec = cans_[ch]->SetConfig(cfg);

    if (fd_supported_ && fdcans_[ch])
    {
      auto &fd_cfg = fd_config_[ch];
      static_cast<CAN::Configuration &>(fd_cfg) = cfg;
      fd_cfg.fd_mode.fd_enabled = (mode.flags & GsUsb::GSCAN_MODE_FD) != 0;

      // 如果已经配置过数据相位，可以下发 FDCAN 配置
      (void)fdcans_[ch]->SetConfig(fd_cfg);
    }

    return ec;
  }

  ErrorCode HandleBerr(uint8_t ch, uint32_t berr_on)
  {
    if (ch >= can_count_)
    {
      return ErrorCode::ARG_ERR;
    }

    berr_enabled_[ch] = (berr_on != 0);
    // 具体错误帧发送逻辑在 OnCanRx(Type::ERROR) 里实现
    return ErrorCode::OK;
  }

  ErrorCode HandleIdentify(uint8_t ch, const GsUsb::Identify &id)
  {
    UNUSED(ch);

    bool on = (id.mode == static_cast<uint32_t>(GsUsb::IdentifyMode::ON));

    if (identify_gpio_)
    {
      (void)identify_gpio_->Write(on);
    }

    return ErrorCode::OK;
  }

  ErrorCode HandleSetTermination(uint8_t ch, const GsUsb::DeviceTerminationState &st)
  {
    if (ch >= can_count_)
    {
      return ErrorCode::ARG_ERR;
    }

    term_state_[ch] = static_cast<GsUsb::TerminationState>(
        st.state != 0 ? static_cast<uint32_t>(GsUsb::TerminationState::ON)
                      : static_cast<uint32_t>(GsUsb::TerminationState::OFF));

    // 如果有一个全局终端电阻 GPIO，则根据任一通道最新状态控制它（最后一次写入为准）
    if (termination_gpio_)
    {
      bool on = (term_state_[ch] == GsUsb::TerminationState::ON);
      (void)termination_gpio_->Write(on);
    }

    return ErrorCode::OK;
  }

  // HostFrame <-> ClassicPack / FDPack 映射
  void HostFrameToClassicPack(const GsUsb::HostFrame &hf, LibXR::CAN::ClassicPack &pack)
  {
    uint32_t cid = hf.can_id;
    bool is_eff = (cid & GsUsb::CAN_EFF_FLAG) != 0;
    bool is_rtr = (cid & GsUsb::CAN_RTR_FLAG) != 0;

    // 这里故意忽略 CAN_ERR_FLAG：主机不应该通过 OUT 发送错误帧到总线

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

    // ClassicPack 支持 dlc：0..8
    uint8_t dlc = hf.can_dlc;
    if (dlc > 8u)
    {
      dlc = 8u;
    }
    pack.dlc = dlc;

    if (dlc > 0u)
    {
      Memory::FastCopy(pack.data, hf.data, dlc);
    }
  }

  void ClassicPackToHostFrame(const LibXR::CAN::ClassicPack &pack, GsUsb::HostFrame &hf)
  {
    // ERROR 类型单独在 OnCanRx 里处理，这里只处理数据/远程帧
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

    hf.echo_id = GsUsb::ECHO_ID_INVALID;
    hf.can_id = cid;

    uint8_t dlc = (pack.dlc <= 8u) ? pack.dlc : 8u;
    hf.can_dlc = dlc;  // classic DLC 0..8 与长度一致

    hf.channel = 0;
    hf.flags = 0;
    hf.reserved = 0;

    if (dlc > 0u)
    {
      Memory::FastCopy(hf.data, pack.data, dlc);
    }
    if (dlc < 8u)
    {
      Memory::FastSet(hf.data + dlc, 0, 8u - dlc);  // padding
    }

    hf.timestamp_us = MakeTimestampUs();
  }

  void HostFrameToFdPack(const GsUsb::HostFrame &hf, LibXR::FDCAN::FDPack &pack)
  {
    uint32_t cid = hf.can_id;
    bool is_eff = (cid & GsUsb::CAN_EFF_FLAG) != 0;
    bool is_rtr = (cid & GsUsb::CAN_RTR_FLAG) != 0;

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

    uint8_t len = DlcToLen(hf.can_dlc);
    if (len > 64)
    {
      len = 64;
    }

    pack.len = len;
    if (len > 0u)
    {
      Memory::FastCopy(pack.data, hf.data, len);
    }
  }

  void FdPackToHostFrame(const LibXR::FDCAN::FDPack &pack, GsUsb::HostFrame &hf)
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

    hf.echo_id = GsUsb::ECHO_ID_INVALID;
    hf.can_id = cid;
    hf.can_dlc = LenToDlc(pack.len);
    hf.channel = 0;
    hf.flags = GsUsb::CAN_FLAG_FD;
    hf.reserved = 0;

    if (pack.len > 0u)
    {
      Memory::FastCopy(hf.data, pack.data, pack.len);
    }

    hf.timestamp_us = MakeTimestampUs();
  }

  // CAN 错误帧映射：ClassicPack(Type::ERROR) -> Linux 风格错误帧 HostFrame
  bool ErrorPackToHostErrorFrame(uint8_t ch, const LibXR::CAN::ClassicPack &err_pack,
                                 GsUsb::HostFrame &hf)
  {
    // ===== 基本校验 =====
    if (ch >= can_count_ || !cans_[ch])
    {
      return false;
    }
    if (!berr_enabled_[ch])
    {
      return false;
    }
    if (!LibXR::CAN::IsErrorId(err_pack.id))
    {
      return false;
    }

    // ===== Linux error.h 里的 data[] 语义常量（避免你头文件没定义导致“空信息”）=====
    // data[1] controller state bits
    constexpr uint8_t LNX_CAN_ERR_CRTL_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_CRTL_RX_WARNING = 0x04;
    constexpr uint8_t LNX_CAN_ERR_CRTL_TX_WARNING = 0x08;
    constexpr uint8_t LNX_CAN_ERR_CRTL_RX_PASSIVE = 0x10;
    constexpr uint8_t LNX_CAN_ERR_CRTL_TX_PASSIVE = 0x20;

    // data[2] protocol error type bits
    constexpr uint8_t LNX_CAN_ERR_PROT_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_PROT_FORM = 0x02;
    constexpr uint8_t LNX_CAN_ERR_PROT_STUFF = 0x04;
    constexpr uint8_t LNX_CAN_ERR_PROT_BIT0 = 0x08;
    constexpr uint8_t LNX_CAN_ERR_PROT_BIT1 = 0x10;
    constexpr uint8_t LNX_CAN_ERR_PROT_TX = 0x80;

    // data[3] protocol error location
    constexpr uint8_t LNX_CAN_ERR_PROT_LOC_UNSPEC = 0x00;
    constexpr uint8_t LNX_CAN_ERR_PROT_LOC_ACK = 0x19;  // ACK slot

    // 可选：新内核使用 CAN_ERR_CNT 表示 data[6]/data[7] 携带计数器
    // 老内核忽略也无害
    constexpr uint32_t LNX_CAN_ERR_CNT = 0x00000200U;

    // ===== 尽量获取错误计数器（用于 data[6]/data[7]，也用于判断 warning/passive
    // 的方向）=====
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

    // ===== 生成 can_id 错误类别 =====
    auto eid = LibXR::CAN::ToErrorID(err_pack.id);

    uint32_t cid = GsUsb::CAN_ERR_FLAG;  // 必须带 CAN_ERR_FLAG

    // 先清空前 8 字节（只清 8，别清 64）
    Memory::FastSet(hf.data, 0, 8);

    switch (eid)
    {
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BUS_OFF:
      {
        cid |= GsUsb::CAN_ERR_BUSOFF;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_WARNING:
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE:
      {
        // Linux 侧：用 CAN_ERR_CRTL + data[1] 表达 warning/passive
        cid |= GsUsb::CAN_ERR_CRTL;

        uint8_t ctrl = LNX_CAN_ERR_CRTL_UNSPEC;

        if (ec_valid)
        {
          // warning 阈值通常是 96；passive 阈值 128（按 CAN 规范/常用实现）
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

          // 如果方向判断不出来（例如计数器实现缺失），至少别让它是 0
          if (ctrl == LNX_CAN_ERR_CRTL_UNSPEC)
          {
            ctrl = (eid == LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE)
                       ? static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_PASSIVE |
                                              LNX_CAN_ERR_CRTL_RX_PASSIVE)
                       : static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_WARNING |
                                              LNX_CAN_ERR_CRTL_RX_WARNING);
          }
        }
        else
        {
          // 没拿到计数器：保守填“可能的状态”
          ctrl = (eid == LibXR::CAN::ErrorID::CAN_ERROR_ID_ERROR_PASSIVE)
                     ? static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_PASSIVE |
                                            LNX_CAN_ERR_CRTL_RX_PASSIVE)
                     : static_cast<uint8_t>(LNX_CAN_ERR_CRTL_TX_WARNING |
                                            LNX_CAN_ERR_CRTL_RX_WARNING);
        }

        hf.data[1] = ctrl;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_ACK:
      {
        // 没有 ACK：用 CAN_ERR_ACK
        cid |= GsUsb::CAN_ERR_ACK;

        // 同时给出“发生在 ACK slot”的位置信息（更利于 candump -e 解码）
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_UNSPEC | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_ACK;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_STUFF:
      {
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_STUFF | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_FORM:
      {
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_FORM | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BIT0:
      {
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_BIT0 | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_BIT1:
      {
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_BIT1 | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
      }

      case LibXR::CAN::ErrorID::CAN_ERROR_ID_CRC:
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_PROTOCOL:
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_GENERIC:
      case LibXR::CAN::ErrorID::CAN_ERROR_ID_OTHER:
      default:
      {
        // 泛化：给 PROT + UNSPEC
        cid |= GsUsb::CAN_ERR_PROT;
        hf.data[2] = static_cast<uint8_t>(LNX_CAN_ERR_PROT_UNSPEC | LNX_CAN_ERR_PROT_TX);
        hf.data[3] = LNX_CAN_ERR_PROT_LOC_UNSPEC;
        break;
      }
    }

    // ===== 填计数器（data[6]=txerr, data[7]=rxerr），并标记 CAN_ERR_CNT =====
    cid |= LNX_CAN_ERR_CNT;
    hf.data[6] = txerr_u8;
    hf.data[7] = rxerr_u8;

    // ===== 输出 HostFrame =====
    hf.echo_id = GsUsb::ECHO_ID_INVALID;
    hf.can_id = cid;
    hf.can_dlc = GsUsb::CAN_ERR_DLC;  // 固定 8
    hf.channel = ch;
    hf.flags = 0;
    hf.reserved = 0;
    hf.timestamp_us = MakeTimestampUs();

    return true;
  }

  // ================= 工具函数 =================

  /// 按当前配置生成 timestamp（仅在 timestamps_enabled_ 时返回非 0）
  uint32_t MakeTimestampUs() const
  {
    if (timestamps_enabled_ && LibXR::Timebase::timebase != nullptr)
    {
      return static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
    }
    return 0u;
  }

  /// 按 flags + timestamp 配置计算 HostFrame 序列化长度
  std::size_t GetHostFrameSize(const GsUsb::HostFrame &hf) const
  {
    const bool is_fd = (hf.flags & GsUsb::CAN_FLAG_FD) != 0;
    if (is_fd)
    {
      return timestamps_enabled_ ? GsUsb::HOST_FRAME_FD_TS_SIZE
                                 : GsUsb::HOST_FRAME_FD_SIZE;
    }
    else
    {
      return timestamps_enabled_ ? GsUsb::HOST_FRAME_CLASSIC_TS_SIZE
                                 : GsUsb::HOST_FRAME_CLASSIC_SIZE;
    }
  }

  // DLC <-> 长度 映射（CAN FD）
  static uint8_t DlcToLen(uint8_t dlc)
  {
    static constexpr uint8_t table[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                          8, 12, 16, 20, 24, 32, 48, 64};
    return (dlc < 16) ? table[dlc] : 64;
  }

  static uint8_t LenToDlc(uint8_t len)
  {
    if (len <= 8)
    {
      return len;
    }
    if (len <= 12)
    {
      return 9;
    }
    if (len <= 16)
    {
      return 10;
    }
    if (len <= 20)
    {
      return 11;
    }
    if (len <= 24)
    {
      return 12;
    }
    if (len <= 32)
    {
      return 13;
    }
    if (len <= 48)
    {
      return 14;
    }
    return 15;
  }

  // ============ USER_ID 的 RAM/Database 存取封装 ============

  uint32_t GetUserIdFromStorage() const
  {
    if constexpr (std::is_void_v<DatabaseType>)
    {
      // 没有数据库：只用 RAM
      return user_id_ram_;
    }
    else
    {
      if (database_ == nullptr)
      {
        return user_id_ram_;
      }

      // 有数据库：通过 DatabaseType::Key<uint32_t> 访问
      typename DatabaseType::template Key<uint32_t> key(*database_, "user_id",
                                                        user_id_ram_);
      return static_cast<uint32_t>(key);
    }
  }

  void SetUserIdToStorage(uint32_t value)
  {
    user_id_ram_ = value;

    if constexpr (!std::is_void_v<DatabaseType>)
    {
      if (database_ == nullptr)
      {
        return;
      }

      // 写入数据库
      typename DatabaseType::template Key<uint32_t> key(*database_, "user_id", 0u);
      key.Set(value);
    }
  }
};

// ============ C++17 模板参数自动推导（CTAD） ============
// 仅针对 std::array + CAN/FDCAN，DatabaseType 默认为 void

template <std::size_t N>
GsUsbClass(const std::array<LibXR::CAN *, N> &cans, Endpoint::EPNumber data_in_ep_num,
           Endpoint::EPNumber data_out_ep_num, LibXR::GPIO *identify_gpio,
           LibXR::GPIO *termination_gpio) -> GsUsbClass<N>;

template <std::size_t N>
GsUsbClass(const std::array<LibXR::FDCAN *, N> &fd_cans,
           Endpoint::EPNumber data_in_ep_num, Endpoint::EPNumber data_out_ep_num,
           LibXR::GPIO *identify_gpio, LibXR::GPIO *termination_gpio) -> GsUsbClass<N>;

}  // namespace LibXR::USB
