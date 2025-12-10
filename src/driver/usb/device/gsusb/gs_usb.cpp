#include "gs_usb.hpp"

#include <cstring>

#include "libxr_def.hpp"
#include "timebase.hpp"  // 假定 LibXR::Timebase 提供 GetMicroseconds()

namespace LibXR::USB
{

// ============ DLC <-> len ============

uint8_t GsUsbClass::DlcToLen(uint8_t dlc)
{
  // 与 Linux CAN FD DLC 编码一致
  static const uint8_t table[16] = {0, 1,  2,  3,  4,  5,  6,  7,
                                    8, 12, 16, 20, 24, 32, 48, 64};
  return (dlc < 16) ? table[dlc] : 64;
}

uint8_t GsUsbClass::LenToDlc(uint8_t len)
{
  if (len <= 8)
  {
    return len;
  }
  if (len <= 12) return 9;
  if (len <= 16) return 10;
  if (len <= 20) return 11;
  if (len <= 24) return 12;
  if (len <= 32) return 13;
  if (len <= 48) return 14;
  return 15;
}

// ============ 构造 ============

GsUsbClass::GsUsbClass(LibXR::CAN& can, Endpoint::EPNumber data_in_ep_num,
                       Endpoint::EPNumber data_out_ep_num, LibXR::GPIO* identify_gpio)
    : data_in_ep_num_(data_in_ep_num),
      data_out_ep_num_(data_out_ep_num),
      identify_gpio_(identify_gpio)
{
  fd_supported_ = false;

  can_count_ = 1;
  cans_[0] = &can;

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

  // Extended BT const（经典 CAN 模式下也可以回复，只是 FD 能力标志关掉）
  bt_const_ext_.feature = bt_const_.feature;  // FD 能力位后面在 FDCAN 构造里开
  bt_const_ext_.fclk_can = fclk;
  bt_const_ext_.btc = bt_const_.btc;
  bt_const_ext_.dbtc = bt_const_.btc;  // 数据相位暂时等同仲裁相位（后面可细化）

  std::memset(config_, 0, sizeof(config_));
  std::memset(fd_config_, 0, sizeof(fd_config_));
}

GsUsbClass::GsUsbClass(std::initializer_list<LibXR::CAN*> cans,
                       Endpoint::EPNumber data_in_ep_num,
                       Endpoint::EPNumber data_out_ep_num, LibXR::GPIO* identify_gpio)
    : data_in_ep_num_(data_in_ep_num),
      data_out_ep_num_(data_out_ep_num),
      identify_gpio_(identify_gpio)
{
  fd_supported_ = false;

  can_count_ =
      static_cast<uint8_t>(std::min(cans.size(), static_cast<size_t>(MAX_CAN_CH)));
  ASSERT(can_count_ > 0);

  size_t i = 0;
  for (auto* p : cans)
  {
    if (i >= can_count_) break;
    cans_[i] = p;
    ++i;
  }

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

  std::memset(config_, 0, sizeof(config_));
  std::memset(fd_config_, 0, sizeof(fd_config_));
}

GsUsbClass::GsUsbClass(std::initializer_list<LibXR::FDCAN*> fd_cans,
                       Endpoint::EPNumber data_in_ep_num,
                       Endpoint::EPNumber data_out_ep_num, LibXR::GPIO* identify_gpio)
    : data_in_ep_num_(data_in_ep_num),
      data_out_ep_num_(data_out_ep_num),
      identify_gpio_(identify_gpio)
{
  fd_supported_ = true;

  can_count_ =
      static_cast<uint8_t>(std::min(fd_cans.size(), static_cast<size_t>(MAX_CAN_CH)));
  ASSERT(can_count_ > 0);

  size_t i = 0;
  for (auto* p : fd_cans)
  {
    if (i >= can_count_) break;
    fdcans_[i] = p;
    cans_[i] = p;  // 向上转成 CAN*
    ++i;
  }

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

  std::memset(config_, 0, sizeof(config_));
  std::memset(fd_config_, 0, sizeof(fd_config_));
}

// ================= ConfigDescriptorItem =================

void GsUsbClass::Init(EndpointPool& endpoint_pool, uint8_t start_itf_num)
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

  SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});

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

  tx_in_progress_.store(false, std::memory_order_release);
  tx_put_index_ = 0;
  tx_get_index_ = 0;

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

  // 预留最大一帧（FD + timestamp）
  ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
}

void GsUsbClass::Deinit(EndpointPool& endpoint_pool)
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

  tx_in_progress_.store(false, std::memory_order_release);

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

bool GsUsbClass::OwnsEndpoint(uint8_t ep_addr) const
{
  if (!inited_) return false;

  return (ep_data_in_ && ep_data_in_->GetAddress() == ep_addr) ||
         (ep_data_out_ && ep_data_out_->GetAddress() == ep_addr);
}

// ================= Vendor Request: SETUP 阶段 =================

ErrorCode GsUsbClass::OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                      uint16_t wLength, uint16_t wIndex,
                                      DeviceClass::RequestResult& result)
{
  UNUSED(in_isr);
  UNUSED(wIndex);  // interface number，一般只有 0

  auto req = static_cast<GsUsb::BReq>(bRequest);

  switch (req)
  {
    // ===== Device -> Host =====
    case GsUsb::BReq::BT_CONST:
    {
      if (wLength < sizeof(bt_const_)) return ErrorCode::ARG_ERR;

      result.write_data =
          ConstRawData{reinterpret_cast<const uint8_t*>(&bt_const_), sizeof(bt_const_)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::BT_CONST_EXT:
    {
      if (!fd_supported_) return ErrorCode::NOT_SUPPORT;

      if (wLength < sizeof(bt_const_ext_)) return ErrorCode::ARG_ERR;

      result.write_data = ConstRawData{reinterpret_cast<const uint8_t*>(&bt_const_ext_),
                                       sizeof(bt_const_ext_)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::DEVICE_CONFIG:
    {
      if (wLength < sizeof(dev_cfg_)) return ErrorCode::ARG_ERR;

      result.write_data =
          ConstRawData{reinterpret_cast<const uint8_t*>(&dev_cfg_), sizeof(dev_cfg_)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::TIMESTAMP:
    {
      uint32_t ts = 0;
      if (LibXR::Timebase::timebase != nullptr)
      {
        ts = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
      }
      ctrl_buf_.timestamp_us = ts;

      if (wLength < sizeof(ctrl_buf_.timestamp_us)) return ErrorCode::ARG_ERR;

      result.write_data =
          ConstRawData{reinterpret_cast<const uint8_t*>(&ctrl_buf_.timestamp_us),
                       sizeof(ctrl_buf_.timestamp_us)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::GET_TERMINATION:
    {
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_buf_.term.state = static_cast<uint32_t>(term_state_[wValue]);

      if (wLength < sizeof(ctrl_buf_.term)) return ErrorCode::ARG_ERR;

      result.write_data = ConstRawData{reinterpret_cast<const uint8_t*>(&ctrl_buf_.term),
                                       sizeof(ctrl_buf_.term)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::GET_STATE:
    {
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      // TODO: 如有底层错误计数器，这里可从底层读取
      ctrl_buf_.dev_state.state = static_cast<uint32_t>(GsUsb::CanState::ERROR_ACTIVE);
      ctrl_buf_.dev_state.rxerr = 0;
      ctrl_buf_.dev_state.txerr = 0;

      if (wLength < sizeof(ctrl_buf_.dev_state)) return ErrorCode::ARG_ERR;

      result.write_data =
          ConstRawData{reinterpret_cast<const uint8_t*>(&ctrl_buf_.dev_state),
                       sizeof(ctrl_buf_.dev_state)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::GET_USER_ID:
    {
      ctrl_buf_.user_id = 0;  // 当前简单返回 0

      if (wLength < sizeof(ctrl_buf_.user_id)) return ErrorCode::ARG_ERR;

      result.write_data =
          ConstRawData{reinterpret_cast<const uint8_t*>(&ctrl_buf_.user_id),
                       sizeof(ctrl_buf_.user_id)};
      return ErrorCode::OK;
    }

      // ===== Host -> Device（有 DATA 阶段） =====

    case GsUsb::BReq::HOST_FORMAT:
    {
      if (wLength != sizeof(GsUsb::HostConfig)) return ErrorCode::ARG_ERR;

      result.read_data = RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.host_cfg),
                                 sizeof(GsUsb::HostConfig)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::BITTIMING:
    {
      if (wLength != sizeof(GsUsb::DeviceBitTiming)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data = RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.bt),
                                 sizeof(GsUsb::DeviceBitTiming)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::DATA_BITTIMING:
    {
      if (!fd_supported_) return ErrorCode::NOT_SUPPORT;

      if (wLength != sizeof(GsUsb::DeviceBitTiming)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data = RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.bt),
                                 sizeof(GsUsb::DeviceBitTiming)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::MODE:
    {
      if (wLength != sizeof(GsUsb::DeviceMode)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data =
          RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.mode), sizeof(GsUsb::DeviceMode)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::BERR:
    {
      if (wLength != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data =
          RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.berr_on), sizeof(uint32_t)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::IDENTIFY:
    {
      if (wLength != sizeof(GsUsb::Identify)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data = RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.identify),
                                 sizeof(GsUsb::Identify)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::SET_TERMINATION:
    {
      if (wLength != sizeof(GsUsb::DeviceTerminationState)) return ErrorCode::ARG_ERR;
      if (wValue >= can_count_) return ErrorCode::ARG_ERR;

      ctrl_target_channel_ = static_cast<uint8_t>(wValue);
      result.read_data = RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.term),
                                 sizeof(GsUsb::DeviceTerminationState)};
      return ErrorCode::OK;
    }

    case GsUsb::BReq::SET_USER_ID:
    {
      if (wLength != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
      result.read_data =
          RawData{reinterpret_cast<uint8_t*>(&ctrl_buf_.user_id), sizeof(uint32_t)};
      return ErrorCode::OK;
    }

    default:
      return ErrorCode::NOT_SUPPORT;
  }
}

// ================= DATA 阶段 =================

ErrorCode GsUsbClass::OnClassData(bool in_isr, uint8_t bRequest,
                                  LibXR::ConstRawData& data)
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
      if (data.size_ != sizeof(GsUsb::DeviceTerminationState)) return ErrorCode::ARG_ERR;
      return HandleSetTermination(ctrl_target_channel_, ctrl_buf_.term);

    case GsUsb::BReq::SET_USER_ID:
      if (data.size_ != sizeof(uint32_t)) return ErrorCode::ARG_ERR;
      // TODO: 实际存储 user_id（持久化）
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

// ================= Vendor 具体逻辑 =================

ErrorCode GsUsbClass::HandleHostFormat(const GsUsb::HostConfig& cfg)
{
  host_format_ok_ = (cfg.byte_order == 0x0000beefu);
  return ErrorCode::OK;
}

ErrorCode GsUsbClass::HandleBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming& bt)
{
  if (!host_format_ok_) return ErrorCode::ARG_ERR;
  if (ch >= can_count_ || !cans_[ch]) return ErrorCode::ARG_ERR;
  if (bt.brp == 0) return ErrorCode::ARG_ERR;

  const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
  const uint32_t tseg2 = bt.phase_seg2;
  const uint32_t tq_num = 1u + tseg1 + tseg2;
  if (tq_num == 0) return ErrorCode::ARG_ERR;

  const uint32_t fclk = cans_[ch]->GetClockFreq();

  auto& cfg = config_[ch];
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
    auto& fd_cfg = fd_config_[ch];
    static_cast<CAN::Configuration&>(fd_cfg) = cfg;
    // data 部分在 DATA_BITTIMING 里配置
  }

  return cans_[ch]->SetConfig(cfg);
}

ErrorCode GsUsbClass::HandleDataBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming& bt)
{
  if (!fd_supported_ || ch >= can_count_ || !fdcans_[ch]) return ErrorCode::NOT_SUPPORT;
  if (bt.brp == 0) return ErrorCode::ARG_ERR;

  const uint32_t tseg1 = bt.prop_seg + bt.phase_seg1;
  const uint32_t tseg2 = bt.phase_seg2;
  const uint32_t tq_num = 1u + tseg1 + tseg2;
  if (tq_num == 0) return ErrorCode::ARG_ERR;

  const uint32_t fclk = fdcans_[ch]->GetClockFreq();

  auto& fd_cfg = fd_config_[ch];
  // 确保 nominal 部分有值：如果没配过，就使用 config_[ch]
  static_cast<CAN::Configuration&>(fd_cfg) = config_[ch];

  fd_cfg.data_timing.brp = bt.brp;
  fd_cfg.data_timing.prop_seg = bt.prop_seg;
  fd_cfg.data_timing.phase_seg1 = bt.phase_seg1;
  fd_cfg.data_timing.phase_seg2 = bt.phase_seg2;
  fd_cfg.data_timing.sjw = bt.sjw;

  fd_cfg.data_bitrate = fclk / (bt.brp * tq_num);
  fd_cfg.data_sample_point = static_cast<float>(1u + tseg1) / static_cast<float>(tq_num);

  return fdcans_[ch]->SetConfig(fd_cfg);
}

ErrorCode GsUsbClass::HandleMode(uint8_t ch, const GsUsb::DeviceMode& mode)
{
  if (!host_format_ok_) return ErrorCode::ARG_ERR;
  if (ch >= can_count_ || !cans_[ch]) return ErrorCode::ARG_ERR;

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

  auto& cfg = config_[ch];
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
    auto& fd_cfg = fd_config_[ch];
    static_cast<CAN::Configuration&>(fd_cfg) = cfg;
    fd_cfg.fd_mode.fd_enabled = (mode.flags & GsUsb::GSCAN_MODE_FD) != 0;

    // 如果已经配置过数据相位，可以下发 FDCAN 配置
    (void)fdcans_[ch]->SetConfig(fd_cfg);
  }

  return ec;
}

ErrorCode GsUsbClass::HandleBerr(uint8_t ch, uint32_t berr_on)
{
  if (ch >= can_count_) return ErrorCode::ARG_ERR;

  berr_enabled_[ch] = (berr_on != 0);
  // TODO：结合底层 CAN 错误状态，生成 CAN_ERR_* 错误帧上报
  return ErrorCode::OK;
}

ErrorCode GsUsbClass::HandleIdentify(uint8_t ch, const GsUsb::Identify& id)
{
  UNUSED(ch);

  bool on = (id.mode == static_cast<uint32_t>(GsUsb::IdentifyMode::ON));

  if (identify_gpio_)
  {
    (void)identify_gpio_->Write(on);
  }

  return ErrorCode::OK;
}

ErrorCode GsUsbClass::HandleSetTermination(uint8_t ch,
                                           const GsUsb::DeviceTerminationState& st)
{
  if (ch >= can_count_) return ErrorCode::ARG_ERR;

  term_state_[ch] = static_cast<GsUsb::TerminationState>(
      st.state != 0 ? static_cast<uint32_t>(GsUsb::TerminationState::ON)
                    : static_cast<uint32_t>(GsUsb::TerminationState::OFF));
  // TODO：如果有硬件终端电阻控制（GPIO / I2C 等），在这里操作
  return ErrorCode::OK;
}

ErrorCode GsUsbClass::HandleGetState(uint8_t ch)
{
  UNUSED(ch);
  // 已经在 OnVendorRequest 里直接填充 ctrl_buf_.dev_state 了
  return ErrorCode::OK;
}

// ================= HostFrame <-> ClassicPack =================

void GsUsbClass::HostFrameToClassicPack(const GsUsb::HostFrame& hf,
                                        LibXR::CAN::ClassicPack& pack)
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

  // ClassicPack 没有 dlc，只能按 8 字节处理
  std::memcpy(pack.data, hf.data, sizeof(pack.data));
}

void GsUsbClass::ClassicPackToHostFrame(const LibXR::CAN::ClassicPack& pack,
                                        GsUsb::HostFrame& hf)
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
  hf.can_dlc = 8;  // classic 固定 8
  hf.channel = 0;
  hf.flags = 0;
  hf.reserved = 0;

  std::memcpy(hf.data, pack.data, 8);

  uint32_t ts = 0;
  if (timestamps_enabled_ && LibXR::Timebase::timebase != nullptr)
  {
    ts = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
  }
  hf.timestamp_us = ts;
}

// ================= HostFrame <-> FDPack =================

void GsUsbClass::HostFrameToFdPack(const GsUsb::HostFrame& hf, LibXR::FDCAN::FDPack& pack)
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
  if (len > 64) len = 64;

  pack.len = len;
  std::memcpy(pack.data, hf.data, len);
}

void GsUsbClass::FdPackToHostFrame(const LibXR::FDCAN::FDPack& pack, GsUsb::HostFrame& hf)
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

  std::memcpy(hf.data, pack.data, pack.len);

  uint32_t ts = 0;
  if (timestamps_enabled_ && LibXR::Timebase::timebase != nullptr)
  {
    ts = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
  }
  hf.timestamp_us = ts;
}

// ================= CAN RX 回调 & BULK IN =================

void GsUsbClass::OnCanRx(bool in_isr, uint8_t ch, const LibXR::CAN::ClassicPack& pack)
{
  UNUSED(in_isr);

  if (ch >= can_count_ || !can_enabled_[ch] || !ep_data_in_) return;

  GsUsb::HostFrame hf{};
  ClassicPackToHostFrame(pack, hf);
  hf.channel = ch;

  EnqueueHostFrame(hf, in_isr);
}

void GsUsbClass::OnFdCanRx(bool in_isr, uint8_t ch, const LibXR::FDCAN::FDPack& pack)
{
  UNUSED(in_isr);

  if (!fd_supported_ || ch >= can_count_ || !fd_enabled_[ch] || !ep_data_in_) return;

  GsUsb::HostFrame hf{};
  FdPackToHostFrame(pack, hf);
  hf.channel = ch;

  // BRS / ESI 按 FD 配置粗略设置（没有 per-frame 信息）
  const auto& fd_cfg = fd_config_[ch];
  if (fd_cfg.fd_mode.brs) hf.flags |= GsUsb::CAN_FLAG_BRS;
  if (fd_cfg.fd_mode.esi) hf.flags |= GsUsb::CAN_FLAG_ESI;

  EnqueueHostFrame(hf, in_isr);
}

bool GsUsbClass::EnqueueHostFrame(const GsUsb::HostFrame& hf, bool in_isr)
{
  UNUSED(in_isr);

  GsUsb::HostFrame copy = hf;
  auto ec = tx_pool_.Put(copy, tx_put_index_);
  if (ec != ErrorCode::OK)
  {
    // pool 满，丢帧；TODO: 可以设置 CAN_FLAG_OVERFLOW 错误计数
    return false;
  }

  // 尝试启动一次发送
  TryKickTx(false);
  return true;
}

void GsUsbClass::TryKickTx(bool in_isr)
{
  UNUSED(in_isr);

  if (!ep_data_in_) return;

  bool expected = false;
  if (!tx_in_progress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
  {
    // 已经有发送在进行
    return;
  }

  GsUsb::HostFrame hf{};
  if (tx_pool_.Get(hf, tx_get_index_) != ErrorCode::OK)
  {
    // 没有待发帧
    tx_in_progress_.store(false, std::memory_order_release);
    return;
  }

  auto buffer = ep_data_in_->GetBuffer();

  // 计算帧长度
  std::size_t len = 0;
  if (hf.flags & GsUsb::CAN_FLAG_FD)
  {
    len = timestamps_enabled_ ? GsUsb::HOST_FRAME_FD_TS_SIZE : GsUsb::HOST_FRAME_FD_SIZE;
  }
  else
  {
    len = timestamps_enabled_ ? GsUsb::HOST_FRAME_CLASSIC_TS_SIZE
                              : GsUsb::HOST_FRAME_CLASSIC_SIZE;
  }

  // padding 模式：仅对非 FD 帧用 MaxPacketSize 对齐（与参考实现一致）
  uint8_t tmp_buf[256];  // 足够放下 1 帧（<=80 字节）
  uint8_t* send_ptr = reinterpret_cast<uint8_t*>(&hf);
  uint16_t send_len = static_cast<uint16_t>(len);

  const uint16_t mps = ep_data_in_->MaxPacketSize();
  if (pad_pkts_to_max_pkt_size_ && !(hf.flags & GsUsb::CAN_FLAG_FD) && len < mps)
  {
    std::memcpy(tmp_buf, &hf, len);
    std::memset(tmp_buf + len, 0, mps - len);
    send_ptr = tmp_buf;
    send_len = mps;
  }

  std::memcpy(buffer.addr_, send_ptr, send_len);
  ep_data_in_->Transfer(send_len);
}

void GsUsbClass::OnDataOutComplete(bool in_isr, ConstRawData& data)
{
  UNUSED(in_isr);

  if (!ep_data_out_) return;

  const std::size_t rxlen = data.size_;
  if (rxlen < GsUsb::HOST_FRAME_CLASSIC_SIZE)
  {
    // 长度不够一个基本 classic 帧，忽略并重新接收
    ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
    return;
  }

  const auto* hf = reinterpret_cast<const GsUsb::HostFrame*>(data.addr_);

  const uint8_t ch = hf->channel;
  if (ch >= can_count_ || !can_enabled_[ch] || !cans_[ch])
  {
    ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
    return;
  }

  const bool is_fd = (hf->flags & GsUsb::CAN_FLAG_FD) != 0;

  if (is_fd)
  {
    if (!fd_supported_ || !fdcans_[ch] || !fd_enabled_[ch])
    {
      // 不支持 FD，丢弃
      ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
      return;
    }

    if (rxlen < GsUsb::HOST_FRAME_FD_SIZE)
    {
      // 长度不够 FD 帧基本数据
      ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
      return;
    }

    LibXR::FDCAN::FDPack pack{};
    HostFrameToFdPack(*hf, pack);

    (void)fdcans_[ch]->AddMessage(pack);

    // echo
    if (hf->echo_id != GsUsb::ECHO_ID_INVALID)
    {
      GsUsb::HostFrame echo = *hf;
      if (timestamps_enabled_ && LibXR::Timebase::timebase != nullptr)
      {
        echo.timestamp_us =
            static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
      }
      EnqueueHostFrame(echo, in_isr);
    }
  }
  else
  {
    // Classic CAN
    LibXR::CAN::ClassicPack pack{};
    HostFrameToClassicPack(*hf, pack);
    (void)cans_[ch]->AddMessage(pack);

    if (hf->echo_id != GsUsb::ECHO_ID_INVALID)
    {
      GsUsb::HostFrame echo = *hf;
      if (timestamps_enabled_ && LibXR::Timebase::timebase != nullptr)
      {
        echo.timestamp_us =
            static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds() & 0xFFFFFFFFu);
      }
      EnqueueHostFrame(echo, in_isr);
    }
  }

  // TODO: 若底层 CAN 控制器可以上报错误中断，这里可结合 berr_enabled_[ch]
  //       生成 CAN_ERR_* 错误帧上报到 host（设置 can_id 的 CAN_ERR_FLAG）。

  ep_data_out_->Transfer(static_cast<uint16_t>(GsUsb::HOST_FRAME_FD_TS_SIZE));
}

void GsUsbClass::OnDataInComplete(bool in_isr, ConstRawData& data)
{
  UNUSED(in_isr);
  UNUSED(data);

  tx_in_progress_.store(false, std::memory_order_release);

  // 看 pool 里还有没有帧，有的话继续发
  TryKickTx(false);
}

}  // namespace LibXR::USB
