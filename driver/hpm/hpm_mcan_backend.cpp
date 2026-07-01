#include "hpm_mcan_backend.hpp"

#if LIBXR_HPM_MCAN_SUPPORTED

#include "hpm_interrupt.h"

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

uint16_t SamplePointToHpmRange(float sample_point, bool upper)
{
  if (sample_point <= 0.0f)
  {
    return upper ? 875U : 750U;
  }

  uint32_t sp = static_cast<uint32_t>(sample_point * 1000.0f);
  if (sp < 1U)
  {
    sp = 1U;
  }
  else if (sp > 999U)
  {
    sp = 999U;
  }

  if (upper)
  {
    const uint32_t high = sp + 25U;
    return static_cast<uint16_t>(high > 999U ? 999U : high);
  }

  return static_cast<uint16_t>(sp > 25U ? sp - 25U : 1U);
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

CAN::ErrorID ConvertMcanProtocolError(mcan_last_err_code_t code, CAN::ErrorID fallback)
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
      return fallback;
  }
}

uint32_t AcquireMcanClock(clock_name_t clock)
{
  clock_add_to_group(clock, 0);
  return clock_get_frequency(clock);
}

ErrorCode ApplyMcanMessageBuffer(MCAN_Type* can, void* msg_buf, uint32_t msg_buf_size)
{
  if (can == nullptr || msg_buf == nullptr || msg_buf_size == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  const uintptr_t msg_buf_addr = reinterpret_cast<uintptr_t>(msg_buf);
  if (msg_buf_addr > UINT32_MAX)
  {
    return ErrorCode::ARG_ERR;
  }

  const mcan_msg_buf_attr_t attr = {static_cast<uint32_t>(msg_buf_addr), msg_buf_size};
  return ConvertMcanStatus(mcan_set_msg_buf_attr(can, &attr));
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

void BuildMcanClassicTxFrame(const CAN::ClassicPack& pack, mcan_tx_frame_t& frame)
{
  std::memset(&frame, 0, sizeof(frame));

  const bool is_ext =
      pack.type == CAN::Type::EXTENDED || pack.type == CAN::Type::REMOTE_EXTENDED;
  const bool is_rtr = pack.type == CAN::Type::REMOTE_STANDARD ||
                      pack.type == CAN::Type::REMOTE_EXTENDED;

  frame.use_ext_id = is_ext ? 1U : 0U;
  frame.rtr = is_rtr ? 1U : 0U;
  frame.dlc = pack.dlc <= 8U ? pack.dlc : 8U;
  frame.canfd_frame = 0U;
  frame.bitrate_switch = 0U;
  frame.error_state_indicator = 0U;

  if (is_ext)
  {
    frame.ext_id = pack.id & 0x1FFFFFFFUL;
  }
  else
  {
    frame.std_id = pack.id & 0x7FFUL;
  }

  if (!is_rtr && frame.dlc > 0U)
  {
    std::memcpy(frame.data_8, pack.data, frame.dlc);
  }
}

void BuildMcanClassicRxPack(const mcan_rx_message_t& frame, CAN::ClassicPack& pack)
{
  std::memset(&pack, 0, sizeof(pack));

  if (frame.use_ext_id != 0U)
  {
    pack.id = frame.ext_id & 0x1FFFFFFFUL;
    pack.type = (frame.rtr != 0U) ? CAN::Type::REMOTE_EXTENDED : CAN::Type::EXTENDED;
  }
  else
  {
    pack.id = frame.std_id & 0x7FFUL;
    pack.type = (frame.rtr != 0U) ? CAN::Type::REMOTE_STANDARD : CAN::Type::STANDARD;
  }

  pack.dlc = frame.dlc <= 8U ? static_cast<uint8_t>(frame.dlc) : 8U;
  if (frame.rtr == 0U && pack.dlc > 0U)
  {
    std::memcpy(pack.data, frame.data_8, pack.dlc);
  }
}

}  // namespace LibXR::detail

#endif
