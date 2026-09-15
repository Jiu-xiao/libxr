#include "hpm_mcan.hpp"

#include <cmath>
#include <cstring>

#if LIBXR_HPM_MCAN_CORE_SUPPORTED
#include "hpm_interrupt.h"
#endif

using namespace LibXR;

#if LIBXR_HPM_MCAN_CORE_SUPPORTED

namespace LibXR::detail
{

inline HpmMcanSharedOwner g_hpm_mcan_shared_owner[MCAN_SOC_MAX_COUNT] = {};

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

void PrepareMcanCommonConfig(mcan_config_t& config, bool enable_canfd)
{
  config.enable_canfd = enable_canfd;
  config.ram_config.enable_rxbuf = false;
  config.ram_config.rxbuf_elem_count = 0U;
  config.interrupt_mask = kMcanInterruptMask;
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

bool AcquireSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind)
{
  ASSERT(index < MCAN_SOC_MAX_COUNT);
  if (g_hpm_mcan_shared_owner[index].kind != HpmMcanOwnerKind::NONE &&
      g_hpm_mcan_shared_owner[index].owner != owner)
  {
    return false;
  }

  g_hpm_mcan_shared_owner[index].kind = kind;
  g_hpm_mcan_shared_owner[index].owner = owner;
  return true;
}

void ReleaseSharedMcanOwnership(uint8_t index, void* owner, HpmMcanOwnerKind kind)
{
  if (index >= MCAN_SOC_MAX_COUNT)
  {
    return;
  }
  if (g_hpm_mcan_shared_owner[index].owner == owner &&
      g_hpm_mcan_shared_owner[index].kind == kind)
  {
    g_hpm_mcan_shared_owner[index] = {};
  }
}

}  // namespace LibXR::detail

ErrorCode HPMCAN::ConvertStatus(hpm_stat_t status)
{
  return detail::ConvertMcanStatus(status);
}

ErrorCode HPMCAN::ValidateConfig(const CAN::Configuration& cfg)
{
  if (cfg.mode.triple_sampling)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (cfg.mode.loopback && cfg.mode.listen_only)
  {
    return ErrorCode::NOT_SUPPORT;
  }
  return ErrorCode::OK;
}

void HPMCAN::EmitErrorFrame(CAN::ErrorID error_id, bool in_isr)
{
  ClassicPack pack{};
  pack.id = FromErrorID(error_id);
  pack.type = Type::ERROR;
  pack.dlc = 0U;
  OnMessage(pack, in_isr);
}

void HPMCAN::Shutdown()
{
  detail::ShutdownMcan(can_, irq_, auto_enable_irq_, detail::kMcanInterruptMask);
  configured_ = false;
  tx_lock_.store(0U, std::memory_order_release);
  tx_pend_.store(0U, std::memory_order_release);
}

bool HPMCAN::HasLowLevelTiming(const CAN::BitTiming& timing)
{
  return detail::HasLowLevelTiming(timing);
}

uint16_t HPMCAN::SamplePointToPermille(float sample_point)
{
  return detail::SamplePointToPermille(sample_point);
}

void HPMCAN::BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame)
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
  frame.dlc = pack.dlc > 8U ? 8U : pack.dlc;
  frame.canfd_frame = 0U;
  frame.bitrate_switch = 0U;
  frame.error_state_indicator = 0U;
  if (frame.rtr == 0U && frame.dlc > 0U)
  {
    std::memcpy(frame.data_8, pack.data, frame.dlc);
  }
}

bool HPMCAN::BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack)
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

  uint8_t len = mcan_get_message_size_from_dlc(static_cast<uint8_t>(frame.dlc));
  if (len > 8U)
  {
    len = 8U;
  }
  pack.dlc = len;
  if (frame.rtr == 0U && len > 0U)
  {
    std::memcpy(pack.data, frame.data_8, len);
  }
  return true;
}

HPMCAN::HPMCAN(LibXRHpmCanType* can, clock_name_t clock, uint8_t index, uint32_t irq,
               bool auto_enable_irq, uint32_t tx_pool_size)
    : can_(can),
      clock_(clock),
      index_(index),
      irq_(irq),
      auto_enable_irq_(auto_enable_irq),
      tx_pool_(tx_pool_size)
{
  ASSERT(tx_pool_size > 0U);
  ASSERT(can_ != nullptr);
  ASSERT(index_ < kMaxInstances);
  ASSERT(detail::AcquireSharedMcanOwnership(index_, this, detail::HpmMcanOwnerKind::CAN));
  ownership_acquired_ = true;
  McanRegistry::Register(index_, this);
}

HPMCAN::~HPMCAN()
{
  Shutdown();
  if (ownership_acquired_)
  {
    McanRegistry::Unregister(index_, this);
    detail::ReleaseSharedMcanOwnership(index_, this, detail::HpmMcanOwnerKind::CAN);
    ownership_acquired_ = false;
  }
}

ErrorCode HPMCAN::SetConfig(const CAN::Configuration& cfg)
{
  if (can_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  ErrorCode cfg_ans = ValidateConfig(cfg);
  if (cfg_ans != ErrorCode::OK)
  {
    return cfg_ans;
  }

  if (!std::isfinite(cfg.sample_point))
  {
    return ErrorCode::ARG_ERR;
  }

  const uint32_t clock_hz = detail::AcquireMcanClock(clock_);
  if (clock_hz == 0U)
  {
    return ErrorCode::INIT_ERR;
  }

  mcan_config_t config{};
  mcan_get_default_config(can_, &config);
  detail::PrepareMcanCommonConfig(config, false);
  config.mode = detail::ConvertMcanMode(cfg.mode);
  config.disable_auto_retransmission = cfg.mode.one_shot;

  if (HasLowLevelTiming(cfg.bit_timing))
  {
    config.use_lowlevel_timing_setting = true;
    detail::ApplyLowLevelTiming(cfg.bit_timing, config.can_timing);
  }
  else
  {
    if (cfg.bitrate == 0U)
    {
      return ErrorCode::ARG_ERR;
    }
    config.use_lowlevel_timing_setting = false;
    config.baudrate = cfg.bitrate;
    const uint16_t sample_point = SamplePointToPermille(cfg.sample_point);
    if (sample_point != 0U)
    {
      config.can20_samplepoint_min = sample_point;
      config.can20_samplepoint_max = sample_point;
    }
  }

  detail::PrepareMcanAcceptAllFilters(config);

  Shutdown();
  ErrorCode ans = detail::ConvertMcanStatus(mcan_init(can_, &config, clock_hz));
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  configured_ = true;
  tx_lock_.store(0U, std::memory_order_release);
  tx_pend_.store(0U, std::memory_order_release);
  detail::EnableMcanInterrupts(can_, irq_, auto_enable_irq_, detail::kMcanInterruptMask);
  return ErrorCode::OK;
}

uint32_t HPMCAN::GetClockFreq() const { return clock_get_frequency(clock_); }

ErrorCode HPMCAN::AddMessage(const ClassicPack& pack)
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

  if (tx_pool_.Push(pack) != ErrorCode::OK)
  {
    TxService();
    if (tx_pool_.Push(pack) != ErrorCode::OK)
    {
      return ErrorCode::FULL;
    }
  }

  TxService();
  return ErrorCode::OK;
}

ErrorCode HPMCAN::GetErrorState(CAN::ErrorState& state) const
{
  return detail::ReadMcanErrorState(can_, state);
}

void HPMCAN::ProcessRx(bool in_isr)
{
  if (!configured_)
  {
    return;
  }
  ProcessRxFifo(0U, in_isr);
  ProcessRxFifo(1U, in_isr);
}

void HPMCAN::ProcessInterrupt(bool in_isr)
{
  detail::ProcessMcanInterrupt(
      can_, configured_, in_isr, [this](uint32_t fifo_index, uint32_t, bool rx_in_isr)
      { ProcessRxFifo(fifo_index, rx_in_isr); }, [this](bool err_in_isr)
      { EmitErrorFrame(ErrorID::CAN_ERROR_ID_OTHER, err_in_isr); }, [this]()
      { TxService(); }, [this](uint32_t, bool err_in_isr) { ProcessError(err_in_isr); });
}

void HPMCAN::OnInterrupt(uint8_t index)
{
  if (index >= kMaxInstances)
  {
    return;
  }
  if (auto* can = McanRegistry::Get(index))
  {
    can->ProcessInterrupt(true);
  }
}

void HPMCAN::TxService()
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

    while (detail::HardwareTxQueueEmptySize(can_) != 0U)
    {
      if (!pending_tx_valid_)
      {
        if (tx_pool_.Pop(pending_tx_) != ErrorCode::OK)
        {
          break;
        }
        pending_tx_valid_ = true;
      }
      mcan_tx_frame_t frame{};
      BuildTxFrame(pending_tx_, frame);

      uint32_t fifo_index = 0U;
      const hpm_stat_t status =
          mcan_transmit_via_txfifo_nonblocking(can_, &frame, &fifo_index);
      UNUSED(fifo_index);
      if (status != status_success)
      {
        break;
      }
      pending_tx_valid_ = false;
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

void HPMCAN::ProcessRxFifo(uint32_t fifo_index, bool in_isr)
{
  detail::DrainMcanRxFifo(
      can_, fifo_index,
      [this, in_isr](const mcan_rx_message_t& rx_frame)
      {
        ClassicPack pack{};
        if (BuildRxPack(rx_frame, pack))
        {
          OnMessage(pack, in_isr);
        }
      },
      []() {});
}

void HPMCAN::ProcessError(bool in_isr)
{
  mcan_protocol_status_t protocol{};
  if (mcan_get_protocol_status(can_, &protocol) != status_success)
  {
    return;
  }

  if (protocol.in_bus_off_state)
  {
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_BUS_OFF, in_isr);
  }
  else if (protocol.in_error_passive_state)
  {
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_ERROR_PASSIVE, in_isr);
  }
  else if (protocol.in_warning_state)
  {
    EmitErrorFrame(ErrorID::CAN_ERROR_ID_ERROR_WARNING, in_isr);
  }
  else
  {
    EmitErrorFrame(detail::ConvertMcanProtocolError(protocol.last_error_code), in_isr);
  }
}

extern "C" void libxr_hpm_can_process_interrupt(uint8_t index)
{
  HPMCAN::OnInterrupt(index);
}

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

HPMCANFD::HPMCANFD(LibXRHpmCanFdType* can, clock_name_t clock, uint8_t index,
                   uint32_t irq, bool auto_enable_irq, uint32_t queue_size)
    : can_(can),
      clock_(clock),
      index_(index),
      irq_(irq),
      auto_enable_irq_(auto_enable_irq),
      tx_pool_(queue_size),
      tx_pool_fd_(queue_size)
{
  ASSERT(queue_size > 0U);
  ASSERT(can_ != nullptr);
  ASSERT(index_ < kMaxInstances);
  ASSERT(
      detail::AcquireSharedMcanOwnership(index_, this, detail::HpmMcanOwnerKind::CANFD));
  ownership_acquired_ = true;
  McanRegistry::Register(index_, this);
  Init();
}

HPMCANFD::~HPMCANFD()
{
  Shutdown();
  if (ownership_acquired_)
  {
    McanRegistry::Unregister(index_, this);
    detail::ReleaseSharedMcanOwnership(index_, this, detail::HpmMcanOwnerKind::CANFD);
    ownership_acquired_ = false;
  }
}

void HPMCANFD::Shutdown()
{
  detail::ShutdownMcan(can_, irq_, auto_enable_irq_, detail::kMcanInterruptMask);
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

  if (!std::isfinite(cfg.sample_point) ||
      (cfg.fd_mode.fd_enabled && !std::isfinite(cfg.data_sample_point)))
  {
    return ErrorCode::ARG_ERR;
  }

  const uint32_t clock_hz = detail::AcquireMcanClock(clock_);
  if (clock_hz == 0U)
  {
    return ErrorCode::INIT_ERR;
  }

  mcan_config_t config{};
  mcan_get_default_config(can_, &config);
  detail::PrepareMcanCommonConfig(config, cfg.fd_mode.fd_enabled);
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

  Shutdown();
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
  detail::EnableMcanInterrupts(can_, irq_, auto_enable_irq_, detail::kMcanInterruptMask);
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

  if (tx_pool_.Push(pack) != ErrorCode::OK)
  {
    TxService();
    if (tx_pool_.Push(pack) != ErrorCode::OK)
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

  if (tx_pool_fd_.Push(pack) != ErrorCode::OK)
  {
    TxService();
    if (tx_pool_fd_.Push(pack) != ErrorCode::OK)
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
  if (index >= kMaxInstances)
  {
    return;
  }
  if (auto* can = McanRegistry::Get(index))
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
      if (fd_enabled_ && !pending_fd_valid_)
      {
        pending_fd_valid_ = tx_pool_fd_.Pop(pending_fd_) == ErrorCode::OK;
      }
      if (fd_enabled_ && pending_fd_valid_)
      {
        BuildTxFrame(pending_fd_, tx_buff_.frame);
        tx_buff_.frame.bitrate_switch = brs_enabled_ ? 1U : 0U;
        tx_buff_.frame.error_state_indicator = esi_enabled_ ? 1U : 0U;

        uint32_t fifo_index = 0U;
        const hpm_stat_t status =
            mcan_transmit_via_txfifo_nonblocking(can_, &tx_buff_.frame, &fifo_index);
        UNUSED(fifo_index);
        if (status != status_success)
        {
          break;
        }
        pending_fd_valid_ = false;
        continue;
      }

      if (!pending_tx_valid_)
      {
        pending_tx_valid_ = tx_pool_.Pop(pending_tx_) == ErrorCode::OK;
      }
      if (pending_tx_valid_)
      {
        BuildTxFrame(pending_tx_, tx_buff_.frame);
        uint32_t fifo_index = 0U;
        const hpm_stat_t status =
            mcan_transmit_via_txfifo_nonblocking(can_, &tx_buff_.frame, &fifo_index);
        UNUSED(fifo_index);
        if (status != status_success)
        {
          break;
        }
        pending_tx_valid_ = false;
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

extern "C" void libxr_hpm_mcan_process_interrupt(uint8_t index)
{
  HPMCANFD::OnInterrupt(index);
}

#else

ErrorCode HPMCANFD::ConvertStatus(hpm_stat_t status)
{
  UNUSED(status);
  return ErrorCode::NOT_SUPPORT;
}

bool HPMCANFD::HasLowLevelTiming(const CAN::BitTiming& timing)
{
  UNUSED(timing);
  return false;
}

bool HPMCANFD::HasLowLevelTiming(const FDCAN::DataBitTiming& timing)
{
  UNUSED(timing);
  return false;
}

uint16_t HPMCANFD::SamplePointToPermilleX10(float sample_point)
{
  UNUSED(sample_point);
  return 0U;
}

uint8_t HPMCANFD::BytesToDlc(uint8_t bytes) { return bytes; }

uint8_t HPMCANFD::DlcToBytes(uint8_t dlc) { return dlc; }

inline void HPMCANFD::BuildTxFrame(const ClassicPack& pack, mcan_tx_frame_t& frame)
{
  UNUSED(pack);
  UNUSED(frame);
}

inline void HPMCANFD::BuildTxFrame(const FDPack& pack, mcan_tx_frame_t& frame)
{
  UNUSED(pack);
  UNUSED(frame);
}

bool HPMCANFD::BuildRxPack(const mcan_rx_message_t& frame, ClassicPack& pack)
{
  UNUSED(frame);
  UNUSED(pack);
  return false;
}

bool HPMCANFD::BuildRxPack(const mcan_rx_message_t& frame, FDPack& pack)
{
  UNUSED(frame);
  UNUSED(pack);
  return false;
}

CAN::ErrorID HPMCANFD::ConvertProtocolError(mcan_last_err_code_t code)
{
  UNUSED(code);
  return ErrorID::CAN_ERROR_ID_GENERIC;
}

void HPMCANFD::EmitErrorFrame(CAN::ErrorID error_id, bool in_isr)
{
  UNUSED(error_id);
  UNUSED(in_isr);
}

HPMCANFD::HPMCANFD(LibXRHpmCanFdType* can, clock_name_t clock, uint8_t index,
                   uint32_t irq, bool auto_enable_irq, uint32_t queue_size)
    : can_(can),
      clock_(clock),
      index_(index),
      irq_(irq),
      auto_enable_irq_(auto_enable_irq),
      tx_pool_(queue_size),
      tx_pool_fd_(queue_size)
{
  ASSERT(queue_size > 0U);
}

HPMCANFD::~HPMCANFD() = default;

void HPMCANFD::Shutdown()
{
  configured_ = false;
  fd_enabled_ = false;
  brs_enabled_ = false;
  esi_enabled_ = false;
  tx_lock_.store(0U, std::memory_order_release);
  tx_pend_.store(0U, std::memory_order_release);
}

ErrorCode HPMCANFD::Init(void) { return ErrorCode::NOT_SUPPORT; }

ErrorCode HPMCANFD::SetConfig(const CAN::Configuration& cfg)
{
  UNUSED(cfg);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMCANFD::SetConfig(const FDCAN::Configuration& cfg)
{
  UNUSED(cfg);
  return ErrorCode::NOT_SUPPORT;
}

uint32_t HPMCANFD::GetClockFreq() const { return 0U; }

ErrorCode HPMCANFD::AddMessage(const ClassicPack& pack)
{
  UNUSED(pack);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMCANFD::AddMessage(const FDPack& pack)
{
  UNUSED(pack);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMCANFD::GetErrorState(CAN::ErrorState& state) const
{
  state = {};
  return ErrorCode::NOT_SUPPORT;
}

size_t HPMCANFD::HardwareTxQueueEmptySize() const { return 0U; }

void HPMCANFD::ProcessRxInterrupt(uint32_t fifo) { UNUSED(fifo); }

void HPMCANFD::ProcessErrorStatusInterrupt(uint32_t error_status_its)
{
  UNUSED(error_status_its);
}

void HPMCANFD::ProcessInterrupt(bool in_isr) { UNUSED(in_isr); }

void HPMCANFD::OnInterrupt(uint8_t index) { UNUSED(index); }

void HPMCANFD::TxService() {}

extern "C" void libxr_hpm_mcan_process_interrupt(uint8_t index) { UNUSED(index); }

#endif

#if LIBXR_HPM_MCAN_CORE_SUPPORTED
namespace
{
void DispatchHpmMcan(uint8_t index)
{
  LibXR::HPMCAN::OnInterrupt(index);
  LibXR::HPMCANFD::OnInterrupt(index);
}
}  // namespace
#if defined(HPM_MCAN0) && defined(IRQn_MCAN0)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN0, libxr_hpm_mcan0_isr)
void libxr_hpm_mcan0_isr(void) { DispatchHpmMcan(0U); }
#endif
#if defined(HPM_MCAN1) && defined(IRQn_MCAN1)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN1, libxr_hpm_mcan1_isr)
void libxr_hpm_mcan1_isr(void) { DispatchHpmMcan(1U); }
#endif
#if defined(HPM_MCAN2) && defined(IRQn_MCAN2)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN2, libxr_hpm_mcan2_isr)
void libxr_hpm_mcan2_isr(void) { DispatchHpmMcan(2U); }
#endif
#if defined(HPM_MCAN3) && defined(IRQn_MCAN3)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN3, libxr_hpm_mcan3_isr)
void libxr_hpm_mcan3_isr(void) { DispatchHpmMcan(3U); }
#endif
#if defined(HPM_MCAN4) && defined(IRQn_MCAN4)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN4, libxr_hpm_mcan4_isr)
void libxr_hpm_mcan4_isr(void) { DispatchHpmMcan(4U); }
#endif
#if defined(HPM_MCAN5) && defined(IRQn_MCAN5)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN5, libxr_hpm_mcan5_isr)
void libxr_hpm_mcan5_isr(void) { DispatchHpmMcan(5U); }
#endif
#if defined(HPM_MCAN6) && defined(IRQn_MCAN6)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN6, libxr_hpm_mcan6_isr)
void libxr_hpm_mcan6_isr(void) { DispatchHpmMcan(6U); }
#endif
#if defined(HPM_MCAN7) && defined(IRQn_MCAN7)
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN7, libxr_hpm_mcan7_isr)
void libxr_hpm_mcan7_isr(void) { DispatchHpmMcan(7U); }
#endif
#endif
