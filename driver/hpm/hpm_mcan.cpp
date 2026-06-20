#include "hpm_mcan.hpp"

#include "hpm_interrupt.h"

using namespace LibXR;

#if LIBXR_HPM_MCAN_SUPPORTED

namespace
{
constexpr uint32_t MCAN_RX_IRQ_MASK =
    MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO0_MSG_LOST |
    MCAN_INT_RXFIFO1_MSG_LOST;

constexpr uint32_t MCAN_TX_IRQ_MASK = MCAN_INT_TX_COMPLETED | MCAN_INT_TXFIFO_EMPTY;

constexpr uint32_t MCAN_ERR_IRQ_MASK =
    MCAN_INT_BUS_OFF_STATUS | MCAN_INT_WARNING_STATUS | MCAN_INT_ERROR_PASSIVE |
    MCAN_INT_PROTOCOL_ERR_IN_ARB_PHASE | MCAN_INT_PROTOCOL_ERR_IN_DATA_PHASE |
    MCAN_INT_MSG_RAM_ACCESS_FAILURE;

static uint16_t sample_point_to_hpm_range(float sample_point, bool upper)
{
  if (sample_point <= 0.0f)
  {
    return upper ? 875u : 750u;
  }

  uint32_t sp = static_cast<uint32_t>(sample_point * 1000.0f);
  if (sp < 1u)
  {
    sp = 1u;
  }
  else if (sp > 999u)
  {
    sp = 999u;
  }

  if (upper)
  {
    uint32_t high = sp + 25u;
    return static_cast<uint16_t>(high > 999u ? 999u : high);
  }

  return static_cast<uint16_t>(sp > 25u ? sp - 25u : 1u);
}
}  // namespace

HPMMCAN::HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size)
    : mcan_(mcan), clock_(clock), irq_(irq), tx_queue_(queue_size)
{
}

HPMMCAN::HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size,
                 void* msg_buf, uint32_t msg_buf_size)
    : mcan_(mcan),
      clock_(clock),
      irq_(irq),
      msg_buf_(msg_buf),
      msg_buf_size_(msg_buf_size),
      tx_queue_(queue_size)
{
}

ErrorCode HPMMCAN::ConvertStatus(LibXRHpmMcanStatus status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_timeout:
      return ErrorCode::TIMEOUT;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_mcan_txfifo_full:
    case status_mcan_txbuf_full:
      return ErrorCode::FULL;
    case status_mcan_rxfifo_empty:
    case status_mcan_rxbuf_empty:
      return ErrorCode::EMPTY;
    case status_mcan_ram_out_of_range:
      return ErrorCode::OUT_OF_RANGE;
    case status_mcan_timeout:
      return ErrorCode::TIMEOUT;
    case status_mcan_invalid_bit_timing:
      return ErrorCode::ARG_ERR;
    default:
      return ErrorCode::FAILED;
  }
}

LibXRHpmMcanMode HPMMCAN::ConvertMode(const CAN::Mode& mode)
{
  if (mode.loopback && mode.listen_only)
  {
    return mcan_mode_loopback_internal;
  }
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

ErrorCode HPMMCAN::SetConfig(const CAN::Configuration& cfg)
{
  if (mcan_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  const ErrorCode msg_buf_status = ApplyMessageBuffer();
  if (msg_buf_status != ErrorCode::OK)
  {
    return msg_buf_status;
  }

  DisableMcanInterrupts();
  tx_lock_.store(0u, std::memory_order_release);
  tx_pend_.store(0u, std::memory_order_release);
  tx_retry_valid_ = false;
  while (tx_queue_.Pop() == ErrorCode::OK)
  {
  }

  mcan_config_t config{};
  mcan_get_default_config(mcan_, &config);

  config.enable_canfd = false;
  config.mode = ConvertMode(cfg.mode);
  config.disable_auto_retransmission = cfg.mode.one_shot;
  config.interrupt_mask = MCAN_RX_IRQ_MASK | MCAN_TX_IRQ_MASK | MCAN_ERR_IRQ_MASK;
  config.txbuf_trans_interrupt_mask = ~0UL;

  const bool use_low_level = cfg.bit_timing.brp != 0u || cfg.bit_timing.prop_seg != 0u ||
                             cfg.bit_timing.phase_seg1 != 0u ||
                             cfg.bit_timing.phase_seg2 != 0u || cfg.bit_timing.sjw != 0u;

  if (use_low_level)
  {
    const uint32_t seg1 = cfg.bit_timing.prop_seg + cfg.bit_timing.phase_seg1;
    if (cfg.bit_timing.brp == 0u || seg1 == 0u || cfg.bit_timing.phase_seg2 == 0u ||
        cfg.bit_timing.sjw == 0u)
    {
      EnableMcanInterrupts();
      return ErrorCode::ARG_ERR;
    }

    config.use_lowlevel_timing_setting = true;
    config.can_timing.prescaler = static_cast<uint16_t>(cfg.bit_timing.brp);
    config.can_timing.num_seg1 = static_cast<uint16_t>(seg1 - 1u);
    config.can_timing.num_seg2 = static_cast<uint16_t>(cfg.bit_timing.phase_seg2);
    config.can_timing.num_sjw = static_cast<uint8_t>(cfg.bit_timing.sjw);
  }
  else if (cfg.bitrate != 0u)
  {
    config.use_lowlevel_timing_setting = false;
    config.baudrate = cfg.bitrate;
    config.can20_samplepoint_min = sample_point_to_hpm_range(cfg.sample_point, false);
    config.can20_samplepoint_max = sample_point_to_hpm_range(cfg.sample_point, true);
  }

  const ErrorCode result = ConvertStatus(mcan_init(mcan_, &config, GetClockFreq()));
  if (result == ErrorCode::OK)
  {
    mcan_disable_standby_pin(mcan_);
    EnableMcanInterrupts();
    (void)EnableInterrupt();
  }
  else
  {
    EnableMcanInterrupts();
  }
  return result;
}

uint32_t HPMMCAN::GetClockFreq() const { return clock_get_frequency(clock_); }

ErrorCode HPMMCAN::SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size)
{
  if (msg_buf == nullptr && msg_buf_size != 0u)
  {
    return ErrorCode::ARG_ERR;
  }

  if (msg_buf != nullptr && msg_buf_size == 0u)
  {
    return ErrorCode::ARG_ERR;
  }

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
  if (msg_buf == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

#if defined(MCAN_MSG_BUF_ALIGNMENT_SIZE)
  if ((reinterpret_cast<uintptr_t>(msg_buf) % MCAN_MSG_BUF_ALIGNMENT_SIZE) != 0u ||
      (msg_buf_size % MCAN_MSG_BUF_ALIGNMENT_SIZE) != 0u)
  {
    return ErrorCode::ARG_ERR;
  }
#endif
#endif

  msg_buf_ = msg_buf;
  msg_buf_size_ = msg_buf_size;
  return ErrorCode::OK;
}

void HPMMCAN::BuildTxFrame(const ClassicPack& pack, LibXRHpmMcanTxFrame& frame)
{
  std::memset(&frame, 0, sizeof(frame));

  const bool IS_EXT = pack.type == Type::EXTENDED || pack.type == Type::REMOTE_EXTENDED;
  const bool IS_RTR =
      pack.type == Type::REMOTE_STANDARD || pack.type == Type::REMOTE_EXTENDED;

  frame.use_ext_id = IS_EXT ? 1u : 0u;
  frame.rtr = IS_RTR ? 1u : 0u;
  frame.dlc = pack.dlc <= 8u ? pack.dlc : 8u;
  frame.canfd_frame = 0u;
  frame.bitrate_switch = 0u;

  if (IS_EXT)
  {
    frame.ext_id = pack.id & 0x1FFFFFFFu;
  }
  else
  {
    frame.std_id = pack.id & 0x7FFu;
  }

  if (!IS_RTR)
  {
    std::memcpy(frame.data_8, pack.data, frame.dlc);
  }
}

void HPMMCAN::BuildRxPack(const LibXRHpmMcanRxFrame& frame, ClassicPack& pack)
{
  const bool IS_EXT = frame.use_ext_id != 0u;
  const bool IS_RTR = frame.rtr != 0u;

  pack.id = IS_EXT ? (frame.ext_id & 0x1FFFFFFFu) : (frame.std_id & 0x7FFu);
  if (IS_RTR)
  {
    pack.type = IS_EXT ? Type::REMOTE_EXTENDED : Type::REMOTE_STANDARD;
  }
  else
  {
    pack.type = IS_EXT ? Type::EXTENDED : Type::STANDARD;
  }

  pack.dlc = frame.dlc <= 8u ? frame.dlc : 8u;
  std::memset(pack.data, 0, sizeof(pack.data));
  if (!IS_RTR)
  {
    std::memcpy(pack.data, frame.data_8, pack.dlc);
  }
}

CAN::ErrorID HPMMCAN::ConvertLastError(uint8_t last_error)
{
  switch (last_error)
  {
    case mcan_last_error_code_stuff_error:
      return ErrorID::CAN_ERROR_ID_STUFF;
    case mcan_last_error_code_format_error:
      return ErrorID::CAN_ERROR_ID_FORM;
    case mcan_last_error_code_ack_error:
      return ErrorID::CAN_ERROR_ID_ACK;
    case mcan_last_error_code_bit1_error:
      return ErrorID::CAN_ERROR_ID_BIT1;
    case mcan_last_error_code_bit0_error:
      return ErrorID::CAN_ERROR_ID_BIT0;
    case mcan_last_error_code_crc_error:
      return ErrorID::CAN_ERROR_ID_CRC;
    default:
      return ErrorID::CAN_ERROR_ID_OTHER;
  }
}

void HPMMCAN::TxService()
{
  if (mcan_ == nullptr)
  {
    return;
  }

  tx_pend_.store(1u, std::memory_order_release);

  uint32_t expected = 0u;
  if (!tx_lock_.compare_exchange_strong(expected, 1u, std::memory_order_acquire,
                                        std::memory_order_relaxed))
  {
    return;
  }

  for (;;)
  {
    tx_pend_.store(0u, std::memory_order_release);

    while (!mcan_is_txfifo_full(mcan_))
    {
      ClassicPack pack{};
      if (tx_retry_valid_)
      {
        pack = tx_retry_pack_;
      }
      else if (tx_queue_.Pop(pack) != ErrorCode::OK)
      {
        break;
      }

      LibXRHpmMcanTxFrame frame{};
      BuildTxFrame(pack, frame);

      uint32_t fifo_index = 0u;
      const LibXRHpmMcanStatus status =
          mcan_transmit_via_txfifo_nonblocking(mcan_, &frame, &fifo_index);
      if (status == status_mcan_txfifo_full)
      {
        tx_retry_pack_ = pack;
        tx_retry_valid_ = true;
        break;
      }
      if (status != status_success)
      {
        tx_retry_valid_ = false;
        break;
      }

      tx_retry_valid_ = false;
    }

    tx_lock_.store(0u, std::memory_order_release);

    if (tx_pend_.load(std::memory_order_acquire) == 0u)
    {
      return;
    }

    expected = 0u;
    if (!tx_lock_.compare_exchange_strong(expected, 1u, std::memory_order_acquire,
                                          std::memory_order_relaxed))
    {
      return;
    }
  }
}

ErrorCode HPMMCAN::AddMessage(const ClassicPack& pack)
{
  if (pack.type == Type::ERROR)
  {
    return ErrorCode::ARG_ERR;
  }

  if (tx_queue_.Push(pack) != ErrorCode::OK)
  {
    return ErrorCode::FULL;
  }

  TxService();
  return ErrorCode::OK;
}

void HPMMCAN::ProcessRxFifo(uint32_t fifo_index)
{
  if (mcan_ == nullptr)
  {
    return;
  }

  for (;;)
  {
    LibXRHpmMcanRxFrame frame{};
    const LibXRHpmMcanStatus status = mcan_read_rxfifo(mcan_, fifo_index, &frame);
    if (status == status_mcan_rxfifo_empty)
    {
      break;
    }

    if (status == status_success)
    {
      ClassicPack pack{};
      BuildRxPack(frame, pack);
      OnMessage(pack, true);
    }
    else
    {
      break;
    }
  }
}

void HPMMCAN::ProcessTx() { TxService(); }

void HPMMCAN::ProcessError()
{
  if (mcan_ == nullptr)
  {
    return;
  }

  mcan_protocol_status_t protocol{};
  if (mcan_get_protocol_status(mcan_, &protocol) != status_success)
  {
    return;
  }

  ClassicPack pack{};
  pack.type = Type::ERROR;

  if (protocol.in_bus_off_state)
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_BUS_OFF);
  }
  else if (protocol.in_error_passive_state)
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_ERROR_PASSIVE);
  }
  else if (protocol.in_warning_state)
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_ERROR_WARNING);
  }
  else
  {
    pack.id = FromErrorID(ConvertLastError(protocol.last_error_code));
  }

  OnMessage(pack, true);
}

void HPMMCAN::ProcessInterrupt()
{
  if (mcan_ == nullptr)
  {
    return;
  }

  const uint32_t flags = mcan_get_interrupt_flags(mcan_);

  if ((flags & (MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_MSG_LOST)) != 0u)
  {
    ProcessRxFifo(0u);
  }
  if ((flags & (MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_MSG_LOST)) != 0u)
  {
    ProcessRxFifo(1u);
  }
  if ((flags & MCAN_TX_IRQ_MASK) != 0u)
  {
    ProcessTx();
  }
  if ((flags & MCAN_ERR_IRQ_MASK) != 0u)
  {
    ProcessError();
  }

  if (flags != 0u)
  {
    mcan_clear_interrupt_flags(mcan_, flags);
  }
}

void HPMMCAN::EnableMcanInterrupts()
{
  if (mcan_ == nullptr)
  {
    return;
  }
  mcan_enable_interrupts(mcan_, MCAN_RX_IRQ_MASK | MCAN_TX_IRQ_MASK | MCAN_ERR_IRQ_MASK);
}

void HPMMCAN::DisableMcanInterrupts()
{
  if (mcan_ == nullptr)
  {
    return;
  }
  mcan_disable_interrupts(mcan_, MCAN_RX_IRQ_MASK | MCAN_TX_IRQ_MASK | MCAN_ERR_IRQ_MASK);
}

ErrorCode HPMMCAN::ApplyMessageBuffer()
{
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
  if (mcan_ == nullptr || msg_buf_ == nullptr || msg_buf_size_ == 0u)
  {
    return ErrorCode::ARG_ERR;
  }

  const uintptr_t msg_buf_addr = reinterpret_cast<uintptr_t>(msg_buf_);
  if (msg_buf_addr > UINT32_MAX)
  {
    return ErrorCode::ARG_ERR;
  }

  const mcan_msg_buf_attr_t attr = {static_cast<uint32_t>(msg_buf_addr), msg_buf_size_};
  return ConvertStatus(mcan_set_msg_buf_attr(mcan_, &attr));
#else
  return ErrorCode::OK;
#endif
}

ErrorCode HPMMCAN::EnableInterrupt()
{
  if (irq_ == INVALID_IRQ)
  {
    return ErrorCode::OK;
  }

  intc_m_enable_irq_with_priority(irq_, 1);
  return ErrorCode::OK;
}

ErrorCode HPMMCAN::DisableInterrupt()
{
  if (irq_ == INVALID_IRQ)
  {
    return ErrorCode::OK;
  }

  intc_m_disable_irq(irq_);
  return ErrorCode::OK;
}

ErrorCode HPMMCAN::GetErrorState(CAN::ErrorState& state) const
{
  if (mcan_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  mcan_error_count_t err{};
  mcan_get_error_counter(mcan_, &err);

  mcan_protocol_status_t protocol{};
  const ErrorCode protocol_status =
      ConvertStatus(mcan_get_protocol_status(mcan_, &protocol));

  state.tx_error_counter = err.transmit_error_count;
  state.rx_error_counter = err.receive_error_count;
  state.bus_off = protocol_status == ErrorCode::OK ? protocol.in_bus_off_state
                                                   : mcan_is_in_busoff_state(mcan_);
  state.error_passive = protocol_status == ErrorCode::OK
                            ? protocol.in_error_passive_state
                            : mcan_is_in_err_passive_state(mcan_);
  state.error_warning = protocol_status == ErrorCode::OK
                            ? protocol.in_warning_state
                            : mcan_is_in_error_warning_state(mcan_);

  return ErrorCode::OK;
}

#else

HPMMCAN::HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size)
    : mcan_(mcan), clock_(clock), irq_(irq), tx_queue_(queue_size)
{
}

HPMMCAN::HPMMCAN(MCAN_Type* mcan, clock_name_t clock, uint32_t irq, uint32_t queue_size,
                 void* msg_buf, uint32_t msg_buf_size)
    : mcan_(mcan),
      clock_(clock),
      irq_(irq),
      msg_buf_(msg_buf),
      msg_buf_size_(msg_buf_size),
      tx_queue_(queue_size)
{
}

ErrorCode HPMMCAN::SetConfig(const CAN::Configuration& cfg)
{
  (void)cfg;
  return ErrorCode::NOT_SUPPORT;
}

uint32_t HPMMCAN::GetClockFreq() const { return 0; }

ErrorCode HPMMCAN::AddMessage(const ClassicPack& pack)
{
  (void)pack;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMMCAN::GetErrorState(CAN::ErrorState& state) const
{
  (void)state;
  return ErrorCode::NOT_SUPPORT;
}

void HPMMCAN::ProcessInterrupt() {}

ErrorCode HPMMCAN::EnableInterrupt() { return ErrorCode::NOT_SUPPORT; }

ErrorCode HPMMCAN::DisableInterrupt() { return ErrorCode::NOT_SUPPORT; }

ErrorCode HPMMCAN::SetMessageBuffer(void* msg_buf, uint32_t msg_buf_size)
{
  (void)msg_buf;
  (void)msg_buf_size;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMMCAN::ConvertStatus(LibXRHpmMcanStatus status)
{
  (void)status;
  return ErrorCode::NOT_SUPPORT;
}

LibXRHpmMcanMode HPMMCAN::ConvertMode(const CAN::Mode& mode)
{
  (void)mode;
  return {};
}

void HPMMCAN::BuildTxFrame(const ClassicPack& pack, LibXRHpmMcanTxFrame& frame)
{
  (void)pack;
  (void)frame;
}

void HPMMCAN::BuildRxPack(const LibXRHpmMcanRxFrame& frame, ClassicPack& pack)
{
  (void)frame;
  (void)pack;
}

CAN::ErrorID HPMMCAN::ConvertLastError(uint8_t last_error)
{
  (void)last_error;
  return ErrorID::CAN_ERROR_ID_GENERIC;
}

void HPMMCAN::EnableMcanInterrupts() {}
void HPMMCAN::DisableMcanInterrupts() {}
ErrorCode HPMMCAN::ApplyMessageBuffer() { return ErrorCode::NOT_SUPPORT; }
void HPMMCAN::TxService() {}
void HPMMCAN::ProcessRxFifo(uint32_t fifo_index) { (void)fifo_index; }
void HPMMCAN::ProcessTx() {}
void HPMMCAN::ProcessError() {}

#endif
