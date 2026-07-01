#include "hpm_canfd.hpp"

#if LIBXR_HPM_MCAN_SUPPORTED

#include <cstring>

#include "hpm_interrupt.h"

using namespace LibXR;

namespace LibXR::detail
{

ErrorCode ConvertMcanStatus(hpm_stat_t status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_timeout:
    case status_mcan_timeout:
      return ErrorCode::TIMEOUT;
    case status_invalid_argument:
    case status_mcan_filter_index_out_of_range:
    case status_mcan_txbuf_index_out_of_range:
    case status_mcan_rxbuf_index_out_of_range:
    case status_mcan_ram_out_of_range:
    case status_mcan_invalid_bit_timing:
      return ErrorCode::ARG_ERR;
    case status_mcan_txfifo_full:
    case status_mcan_txbuf_full:
      return ErrorCode::FULL;
    case status_mcan_rxfifo_empty:
    case status_mcan_rxbuf_empty:
    case status_mcan_tx_evt_fifo_empty:
      return ErrorCode::EMPTY;
    case status_mcan_rxfifo_full:
    case status_mcan_rxfifo0_busy:
    case status_mcan_rxfifo1_busy:
      return ErrorCode::BUSY;
    default:
      return ErrorCode::FAILED;
  }
}

bool HasLowLevelTiming(const CAN::BitTiming& timing)
{
  return timing.brp != 0U && timing.phase_seg1 != 0U && timing.phase_seg2 != 0U &&
         timing.sjw != 0U;
}

bool HasLowLevelTiming(const FDCAN::DataBitTiming& timing)
{
  return timing.brp != 0U && timing.phase_seg1 != 0U && timing.phase_seg2 != 0U &&
         timing.sjw != 0U;
}

uint16_t SamplePointToPermille(float sample_point)
{
  if (sample_point <= 0.0f)
  {
    return 0U;
  }
  if (sample_point > 1.0f)
  {
    sample_point = 1.0f;
  }
  return static_cast<uint16_t>(sample_point * 1000.0f);
}

mcan_node_mode_t ConvertMcanMode(const CAN::Mode& mode)
{
  if (mode.loopback)
  {
    return mcan_mode_loopback_internal;
  }
  if (mode.listen_only)
  {
    return mcan_mode_listen_only;
  }
  return mcan_mode_normal;
}

void ApplyLowLevelTiming(const CAN::BitTiming& src, mcan_bit_timing_param_t& dst)
{
  dst.prescaler = static_cast<uint16_t>(src.brp);
  dst.num_seg1 = static_cast<uint16_t>(src.prop_seg + src.phase_seg1);
  dst.num_seg2 = static_cast<uint16_t>(src.phase_seg2);
  dst.num_sjw = static_cast<uint8_t>(src.sjw);
  dst.enable_tdc = false;
}

void ApplyLowLevelTiming(const FDCAN::DataBitTiming& src, mcan_bit_timing_param_t& dst)
{
  dst.prescaler = static_cast<uint16_t>(src.brp);
  dst.num_seg1 = static_cast<uint16_t>(src.prop_seg + src.phase_seg1);
  dst.num_seg2 = static_cast<uint16_t>(src.phase_seg2);
  dst.num_sjw = static_cast<uint8_t>(src.sjw);
  dst.enable_tdc = false;
}

CAN::ErrorID ConvertMcanProtocolError(mcan_last_err_code_t code)
{
  switch (code)
  {
    case mcan_last_error_code_stuff_error:
      return CAN::ErrorID::CAN_ERROR_ID_STUFF;
    case mcan_last_error_code_format_error:
      return CAN::ErrorID::CAN_ERROR_ID_FORM;
    case mcan_last_error_code_ack_error:
      return CAN::ErrorID::CAN_ERROR_ID_ACK;
    case mcan_last_error_code_bit1_error:
      return CAN::ErrorID::CAN_ERROR_ID_BIT1;
    case mcan_last_error_code_bit0_error:
      return CAN::ErrorID::CAN_ERROR_ID_BIT0;
    case mcan_last_error_code_crc_error:
      return CAN::ErrorID::CAN_ERROR_ID_CRC;
    case mcan_last_error_code_no_error:
    case mcan_last_error_code_no_change:
    default:
      return CAN::ErrorID::CAN_ERROR_ID_GENERIC;
  }
}

uint32_t AcquireMcanClock(clock_name_t clock)
{
  clock_add_to_group(clock, 0);
  return clock_get_frequency(clock);
}

void PrepareMcanCommonConfig(MCAN_Type* can, mcan_config_t& config, bool enable_canfd)
{
  config.enable_canfd = enable_canfd;
  if (enable_canfd)
  {
    mcan_get_default_ram_config(can, &config.ram_config, true);
  }
  config.ram_config.enable_rxbuf = false;
  config.ram_config.rxbuf_elem_count = 0U;
  config.interrupt_mask = MCAN_INTERRUPT_MASK;
}

void PrepareMcanAcceptAllFilters(mcan_config_t& config)
{
  config.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
      MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
  config.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
      MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
  config.all_filters_config.global_filter_config.reject_remote_std_frame = false;
  config.all_filters_config.global_filter_config.reject_remote_ext_frame = false;
}

void ShutdownMcan(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                  uint32_t interrupt_mask)
{
  if (auto_enable_irq && irq != 0xFFFFFFFFUL)
  {
    intc_m_disable_irq(irq);
  }
  if (can == nullptr)
  {
    return;
  }

  mcan_disable_interrupts(can, interrupt_mask);
  mcan_clear_interrupt_flags(can, 0xFFFFFFFFUL);
  mcan_deinit(can);
}

void EnableMcanInterrupts(MCAN_Type* can, uint32_t irq, bool auto_enable_irq,
                          uint32_t interrupt_mask)
{
  if (!auto_enable_irq || irq == 0xFFFFFFFFUL || can == nullptr)
  {
    return;
  }

  mcan_clear_interrupt_flags(can, 0xFFFFFFFFUL);
  mcan_enable_interrupts(can, interrupt_mask);
  intc_m_enable_irq_with_priority(irq, 1);
}

ErrorCode ReadMcanErrorState(MCAN_Type* can, CAN::ErrorState& state)
{
  state = {};
  if (can == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  mcan_error_count_t count{};
  mcan_protocol_status_t protocol{};
  mcan_get_error_counter(can, &count);
  ErrorCode ans = ConvertMcanStatus(mcan_get_protocol_status(can, &protocol));
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  state.tx_error_counter = count.transmit_error_count;
  state.rx_error_counter = count.receive_error_count;
  state.bus_off = protocol.in_bus_off_state;
  state.error_passive = protocol.in_error_passive_state || count.receive_error_passive;
  state.error_warning = protocol.in_warning_state;
  return ErrorCode::OK;
}

size_t HardwareTxQueueEmptySize(MCAN_Type* can)
{
  if (can == nullptr)
  {
    return 0U;
  }
  return static_cast<size_t>(MCAN_TXFQS_TFFL_GET(can->TXFQS));
}

}  // namespace LibXR::detail

HPMCANFD* HPMCANFD::instance_map_[HPMCANFD::MAX_INSTANCES] = {};

ErrorCode HPMCANFD::ConvertStatus(hpm_stat_t status)
{
  return detail::ConvertMcanStatus(status);
}

bool HPMCANFD::HasLowLevelTiming(const CAN::BitTiming& timing)
{
  return detail::HasLowLevelTiming(timing);
}

bool HPMCANFD::HasLowLevelTiming(const FDCAN::DataBitTiming& timing)
{
  return detail::HasLowLevelTiming(timing);
}

uint16_t HPMCANFD::SamplePointToPermilleX10(float sample_point)
{
  return detail::SamplePointToPermille(sample_point);
}

uint8_t HPMCANFD::BytesToDlc(uint8_t bytes)
{
  if (bytes <= 8U)
  {
    return bytes;
  }
  if (bytes <= 12U)
  {
    return MCAN_MSG_DLC_12_BYTES;
  }
  if (bytes <= 16U)
  {
    return MCAN_MSG_DLC_16_BYTES;
  }
  if (bytes <= 20U)
  {
    return MCAN_MSG_DLC_20_BYTES;
  }
  if (bytes <= 24U)
  {
    return MCAN_MSG_DLC_24_BYTES;
  }
  if (bytes <= 32U)
  {
    return MCAN_MSG_DLC_32_BYTES;
  }
  if (bytes <= 48U)
  {
    return MCAN_MSG_DLC_48_BYTES;
  }
  return MCAN_MSG_DLC_64_BYTES;
}

uint8_t HPMCANFD::DlcToBytes(uint8_t dlc) { return mcan_get_message_size_from_dlc(dlc); }

inline void HPMCANFD::BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame)
{
  std::memset(&frame, 0, sizeof(frame));
  frame.use_ext_id =
      (pack.type == Type::EXTENDED || pack.type == Type::REMOTE_EXTENDED) ? 1U : 0U;
  frame.rtr = (pack.type == Type::REMOTE_STANDARD || pack.type == Type::REMOTE_EXTENDED)
                  ? 1U
                  : 0U;
  if (frame.use_ext_id != 0U)
  {
    frame.ext_id = pack.id & 0x1FFFFFFFUL;
  }
  else
  {
    frame.std_id = pack.id & 0x7FFUL;
  }
  frame.dlc = BytesToDlc(pack.dlc > 8U ? 8U : pack.dlc);
  frame.canfd_frame = 0U;
  frame.bitrate_switch = 0U;
  frame.error_state_indicator = 0U;
  if (frame.rtr == 0U && pack.dlc > 0U)
  {
    std::memcpy(frame.data_8, pack.data, pack.dlc > 8U ? 8U : pack.dlc);
  }
}

inline void HPMCANFD::BuildTxFrame(const FDPack& pack, mcan_tx_frame_t& frame)
{
  std::memset(&frame, 0, sizeof(frame));
  frame.use_ext_id = (pack.type == Type::EXTENDED) ? 1U : 0U;
  frame.rtr = 0U;
  if (frame.use_ext_id != 0U)
  {
    frame.ext_id = pack.id & 0x1FFFFFFFUL;
  }
  else
  {
    frame.std_id = pack.id & 0x7FFUL;
  }
  frame.dlc = BytesToDlc(pack.len);
  frame.canfd_frame = 1U;
  frame.bitrate_switch = 0U;
  frame.error_state_indicator = 0U;
  if (pack.len > 0U)
  {
    std::memcpy(frame.data_8, pack.data, pack.len);
  }
}

bool HPMCANFD::BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack)
{
  std::memset(&pack, 0, sizeof(pack));
  if (frame.canfd_frame != 0U)
  {
    return false;
  }

  if (frame.use_ext_id != 0U)
  {
    pack.id = frame.ext_id;
    pack.type = (frame.rtr != 0U) ? Type::REMOTE_EXTENDED : Type::EXTENDED;
  }
  else
  {
    pack.id = frame.std_id;
    pack.type = (frame.rtr != 0U) ? Type::REMOTE_STANDARD : Type::STANDARD;
  }

  uint8_t bytes = DlcToBytes(static_cast<uint8_t>(frame.dlc));
  if (bytes > 8U)
  {
    bytes = 8U;
  }
  pack.dlc = bytes;
  if (frame.rtr == 0U && bytes > 0U)
  {
    std::memcpy(pack.data, frame.data_8, bytes);
  }
  return true;
}

bool HPMCANFD::BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack)
{
  std::memset(&pack, 0, sizeof(pack));
  if (frame.canfd_frame == 0U || frame.rtr != 0U)
  {
    return false;
  }

  if (frame.use_ext_id != 0U)
  {
    pack.id = frame.ext_id;
    pack.type = Type::EXTENDED;
  }
  else
  {
    pack.id = frame.std_id;
    pack.type = Type::STANDARD;
  }

  pack.len = DlcToBytes(static_cast<uint8_t>(frame.dlc));
  if (pack.len > 64U)
  {
    pack.len = 64U;
  }
  if (pack.len > 0U)
  {
    std::memcpy(pack.data, frame.data_8, pack.len);
  }
  return true;
}

CAN::ErrorID HPMCANFD::ConvertProtocolError(mcan_last_err_code_t code)
{
  return detail::ConvertMcanProtocolError(code);
}

void HPMCANFD::EmitErrorFrame(CAN::ErrorID error_id, bool in_isr)
{
  ClassicPack pack{};
  pack.id = FromErrorID(error_id);
  pack.type = Type::ERROR;
  pack.dlc = 0U;
  OnMessage(pack, in_isr);
}

HPMCANFD::HPMCANFD(MCAN_Type* can, clock_name_t clock, uint8_t index, uint32_t irq,
                   bool auto_enable_irq, uint32_t queue_size, void* msg_buf,
                   uint32_t msg_buf_size)
    : can_(can),
      clock_(clock),
      index_(index),
      irq_(irq),
      auto_enable_irq_(auto_enable_irq),
      msg_buf_(msg_buf),
      msg_buf_size_(msg_buf_size),
      tx_pool_(queue_size),
      tx_pool_fd_(queue_size)
{
  REQUIRE(queue_size > 0U);
  REQUIRE(can_ != nullptr);
  REQUIRE(index_ < MAX_INSTANCES);
  REQUIRE(instance_map_[index_] == nullptr);
  instance_map_[index_] = this;
  Init();
}

HPMCANFD::~HPMCANFD()
{
  Shutdown();
  if (index_ < MAX_INSTANCES && instance_map_[index_] == this)
  {
    instance_map_[index_] = nullptr;
  }
}

void HPMCANFD::Shutdown()
{
  detail::ShutdownMcan(can_, irq_, auto_enable_irq_, detail::MCAN_INTERRUPT_MASK);
  configured_ = false;
  fd_enabled_ = false;
  brs_enabled_ = false;
  esi_enabled_ = false;
  tx_lock_.store(0U, std::memory_order_release);
  tx_pend_.store(0U, std::memory_order_release);
}

ErrorCode HPMCANFD::Init(void)
{
  if (can_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  return ErrorCode::OK;
}

ErrorCode HPMCANFD::SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size)
{
  if (msg_buf == nullptr || msg_buf_size == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  msg_buf_ = msg_buf;
  msg_buf_size_ = msg_buf_size;
  return ErrorCode::OK;
}

ErrorCode HPMCANFD::ApplyMessageBuffer()
{
  if (can_ == nullptr || msg_buf_ == nullptr || msg_buf_size_ == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  const uintptr_t msg_buf_addr = reinterpret_cast<uintptr_t>(msg_buf_);
  if (msg_buf_addr > UINT32_MAX)
  {
    return ErrorCode::ARG_ERR;
  }

  const mcan_msg_buf_attr_t attr = {static_cast<uint32_t>(msg_buf_addr), msg_buf_size_};
  return detail::ConvertMcanStatus(mcan_set_msg_buf_attr(can_, &attr));
}

ErrorCode HPMCANFD::SetConfig(const CAN::Configuration& cfg)
{
  FDCAN::Configuration fd_cfg{};
  fd_cfg.bitrate = cfg.bitrate;
  fd_cfg.sample_point = cfg.sample_point;
  fd_cfg.bit_timing = cfg.bit_timing;
  fd_cfg.mode = cfg.mode;
  return SetConfig(fd_cfg);
}

ErrorCode HPMCANFD::SetConfig(const FDCAN::Configuration& cfg)
{
  if (can_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (cfg.mode.triple_sampling)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (cfg.mode.loopback && cfg.mode.listen_only)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (cfg.fd_mode.esi && !cfg.fd_mode.fd_enabled)
  {
    return ErrorCode::ARG_ERR;
  }
  if (cfg.fd_mode.brs && !cfg.fd_mode.fd_enabled)
  {
    return ErrorCode::ARG_ERR;
  }

  Shutdown();

  const ErrorCode msg_buf_status = ApplyMessageBuffer();
  if (msg_buf_status != ErrorCode::OK)
  {
    return msg_buf_status;
  }

  const uint32_t clock_hz = detail::AcquireMcanClock(clock_);
  if (clock_hz == 0U)
  {
    return ErrorCode::INIT_ERR;
  }

  mcan_config_t config{};
  mcan_get_default_config(can_, &config);
  detail::PrepareMcanCommonConfig(can_, config, cfg.fd_mode.fd_enabled);
  config.mode = detail::ConvertMcanMode(cfg.mode);
  config.disable_auto_retransmission = cfg.mode.one_shot;
  config.enable_restricted_operation_mode = cfg.mode.listen_only;
  config.enable_non_iso_mode = false;
  config.enable_tdc = cfg.fd_mode.fd_enabled && cfg.fd_mode.brs;

  const bool nominal_low_level = HasLowLevelTiming(cfg.bit_timing);
  const bool data_low_level = HasLowLevelTiming(cfg.data_timing);

  if (nominal_low_level || data_low_level)
  {
    if (!nominal_low_level)
    {
      return ErrorCode::ARG_ERR;
    }
    if (cfg.fd_mode.fd_enabled && !data_low_level &&
        (cfg.data_bitrate != 0U || cfg.data_sample_point > 0.0f))
    {
      return ErrorCode::ARG_ERR;
    }
    config.use_lowlevel_timing_setting = true;
    detail::ApplyLowLevelTiming(cfg.bit_timing, config.can_timing);
    if (cfg.fd_mode.fd_enabled)
    {
      if (data_low_level)
      {
        detail::ApplyLowLevelTiming(cfg.data_timing, config.canfd_timing);
      }
      else
      {
        config.canfd_timing = config.can_timing;
      }
      config.canfd_timing.enable_tdc = config.enable_tdc;
    }
  }
  else
  {
    if (cfg.bitrate == 0U)
    {
      return ErrorCode::ARG_ERR;
    }
    config.use_lowlevel_timing_setting = false;
    config.baudrate = cfg.bitrate;
    const uint16_t sample_point = SamplePointToPermilleX10(cfg.sample_point);
    if (sample_point != 0U)
    {
      config.can20_samplepoint_min = sample_point;
      config.can20_samplepoint_max = sample_point;
    }

    if (cfg.fd_mode.fd_enabled)
    {
      config.baudrate_fd = (cfg.data_bitrate != 0U) ? cfg.data_bitrate : cfg.bitrate;
      const uint16_t data_sample_point = SamplePointToPermilleX10(cfg.data_sample_point);
      if (data_sample_point != 0U)
      {
        config.canfd_samplepoint_min = data_sample_point;
        config.canfd_samplepoint_max = data_sample_point;
      }
    }
  }

  detail::PrepareMcanAcceptAllFilters(config);

  ErrorCode ans = detail::ConvertMcanStatus(mcan_init(can_, &config, clock_hz));
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  configured_ = true;
  fd_enabled_ = cfg.fd_mode.fd_enabled;
  brs_enabled_ = cfg.fd_mode.brs;
  esi_enabled_ = cfg.fd_mode.esi;
  tx_lock_.store(0U, std::memory_order_release);
  tx_pend_.store(0U, std::memory_order_release);
  detail::EnableMcanInterrupts(can_, irq_, auto_enable_irq_, detail::MCAN_INTERRUPT_MASK);
  return ErrorCode::OK;
}

uint32_t HPMCANFD::GetClockFreq() const { return clock_get_frequency(clock_); }

ErrorCode HPMCANFD::AddMessage(const ClassicPack& pack)
{
  if (can_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (!configured_)
  {
    return ErrorCode::INIT_ERR;
  }
  if (pack.type == Type::ERROR || pack.type == Type::TYPE_NUM || pack.dlc > 8U)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_pool_.Put(pack) != ErrorCode::OK)
  {
    TxService();
    if (tx_pool_.Put(pack) != ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }
  }

  TxService();
  return ErrorCode::OK;
}

ErrorCode HPMCANFD::AddMessage(const FDPack& pack)
{
  if (can_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (!configured_)
  {
    return ErrorCode::INIT_ERR;
  }
  if (!fd_enabled_)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (pack.type != Type::STANDARD && pack.type != Type::EXTENDED)
  {
    return ErrorCode::ARG_ERR;
  }
  if (pack.len > 64U)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_pool_fd_.Put(pack) != ErrorCode::OK)
  {
    TxService();
    if (tx_pool_fd_.Put(pack) != ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }
  }

  TxService();
  return ErrorCode::OK;
}

ErrorCode HPMCANFD::GetErrorState(CAN::ErrorState& state) const
{
  return detail::ReadMcanErrorState(can_, state);
}

size_t HPMCANFD::HardwareTxQueueEmptySize() const
{
  return detail::HardwareTxQueueEmptySize(can_);
}

void HPMCANFD::ProcessRxInterrupt(uint32_t fifo)
{
  if (!configured_ || can_ == nullptr)
  {
    return;
  }

  detail::DrainMcanRxFifo(
      can_, fifo,
      [this](const mcan_rx_message_t& frame)
      {
        rx_buff_.frame = frame;
        if (BuildRxPack(rx_buff_.frame, rx_buff_.pack_fd))
        {
          OnMessage(rx_buff_.pack_fd, true);
          return;
        }

        if (BuildRxPack(rx_buff_.frame, rx_buff_.pack))
        {
          OnMessage(rx_buff_.pack, true);
        }
      },
      [this]() { EmitErrorFrame(ErrorID::CAN_ERROR_ID_OTHER, true); });
}

void HPMCANFD::ProcessErrorStatusInterrupt(uint32_t error_status_its)
{
  if (can_ == nullptr)
  {
    return;
  }

  mcan_protocol_status_t protocol{};
  if (mcan_get_protocol_status(can_, &protocol) != status_success)
  {
    return;
  }

  if ((error_status_its & MCAN_INT_BUS_OFF_STATUS) != 0U && protocol.in_bus_off_state)
  {
    // Bus-off sets CCCR.INIT; clear it so MCAN can run the recovery sequence.
    mcan_enter_normal_mode(can_);
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_BUS_OFF, true);
    return;
  }
  if ((error_status_its & MCAN_INT_ERROR_PASSIVE) != 0U &&
      protocol.in_error_passive_state)
  {
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_ERROR_PASSIVE, true);
    return;
  }
  if ((error_status_its & MCAN_INT_WARNING_STATUS) != 0U && protocol.in_warning_state)
  {
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_ERROR_WARNING, true);
    return;
  }
  if ((error_status_its &
       (MCAN_INT_PROTOCOL_ERR_IN_ARB_PHASE | MCAN_INT_PROTOCOL_ERR_IN_DATA_PHASE |
        MCAN_INT_BIT_ERROR_UNCORRECTED)) != 0U)
  {
    EmitErrorFrame(ConvertProtocolError(protocol.last_error_code), true);
  }
}

void HPMCANFD::ProcessInterrupt(bool in_isr)
{
  detail::ProcessMcanInterrupt(
      can_, configured_, in_isr, [this](uint32_t fifo_index, uint32_t, bool)
      { ProcessRxInterrupt(fifo_index); }, [this](bool err_in_isr)
      { EmitErrorFrame(ErrorID::CAN_ERROR_ID_OTHER, err_in_isr); },
      [this]() { TxService(); },
      [this](uint32_t error_flags, bool) { ProcessErrorStatusInterrupt(error_flags); });
}

void HPMCANFD::OnInterrupt(uint8_t index)
{
  if (index >= MAX_INSTANCES)
  {
    return;
  }
  if (auto* can = instance_map_[index])
  {
    can->ProcessInterrupt(true);
  }
}

void HPMCANFD::TxService()
{
  if (!configured_ || can_ == nullptr)
  {
    return;
  }

  tx_pend_.store(1U, std::memory_order_release);

  uint32_t expected = 0U;
  if (!tx_lock_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
                                        std::memory_order_relaxed))
  {
    return;
  }

  for (;;)
  {
    tx_pend_.store(0U, std::memory_order_release);

    while (HardwareTxQueueEmptySize() != 0U)
    {
      FDPack pfd{};
      if (fd_enabled_ && tx_pool_fd_.Get(pfd) == ErrorCode::OK)
      {
        BuildTxFrame(pfd, tx_buff_.frame);
        tx_buff_.frame.bitrate_switch = brs_enabled_ ? 1U : 0U;
        tx_buff_.frame.error_state_indicator = esi_enabled_ ? 1U : 0U;

        uint32_t fifo_index = 0U;
        const hpm_stat_t status =
            mcan_transmit_via_txfifo_nonblocking(can_, &tx_buff_.frame, &fifo_index);
        UNUSED(fifo_index);
        if (status != status_success)
        {
          if (tx_pool_fd_.Put(pfd) != ErrorCode::OK)
          {
            ASSERT(false);
          }
          break;
        }
        continue;
      }

      ClassicPack pc{};
      if (tx_pool_.Get(pc) == ErrorCode::OK)
      {
        BuildTxFrame(pc, tx_buff_.frame);
        uint32_t fifo_index = 0U;
        const hpm_stat_t status =
            mcan_transmit_via_txfifo_nonblocking(can_, &tx_buff_.frame, &fifo_index);
        UNUSED(fifo_index);
        if (status != status_success)
        {
          if (tx_pool_.Put(pc) != ErrorCode::OK)
          {
            ASSERT(false);
          }
          break;
        }
        continue;
      }

      break;
    }

    tx_lock_.store(0U, std::memory_order_release);
    if (tx_pend_.load(std::memory_order_acquire) == 0U)
    {
      return;
    }

    expected = 0U;
    if (!tx_lock_.compare_exchange_strong(expected, 1U, std::memory_order_acquire,
                                          std::memory_order_relaxed))
    {
      return;
    }
  }
}

#endif
