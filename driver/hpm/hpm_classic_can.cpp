#include "hpm_classic_can.hpp"

#include "hpm_interrupt.h"

using namespace LibXR;

#if LIBXR_HPM_CLASSIC_CAN_SUPPORTED

namespace
{
constexpr uint8_t TX_RX_IRQ_MASK =
    CAN_EVENT_RECEIVE | CAN_EVENT_RX_BUF_OVERRUN | CAN_EVENT_TX_SECONDARY_BUF;

constexpr uint8_t ERROR_IRQ_MASK = CAN_ERROR_PASSIVE_INT_ENABLE |
                                   CAN_ERROR_ARBITRATION_LOST_INT_ENABLE |
                                   CAN_ERROR_BUS_ERROR_INT_ENABLE;

constexpr uint8_t ERROR_FLAG_MASK =
    CAN_ERROR_PASSIVE_INT_FLAG | CAN_ERROR_ARBITRATION_LOST_INT_FLAG |
    CAN_ERROR_BUS_ERROR_INT_FLAG | CAN_ERROR_PASSIVE_MODE_ACTIVE_FLAG |
    CAN_ERROR_WARNING_LIMIT_FLAG;

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

HPMClassicCAN::HPMClassicCAN(CAN_Type* can, clock_name_t clock, uint32_t irq, uint32_t queue_size)
    : can_(can), clock_(clock), irq_(irq), tx_queue_(queue_size)
{
}

ErrorCode HPMClassicCAN::ConvertStatus(LibXRHpmClassicCanStatus status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_timeout:
      return ErrorCode::TIMEOUT;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_can_tx_fifo_full:
      return ErrorCode::FULL;
    default:
      return ErrorCode::FAILED;
  }
}

LibXRHpmClassicCanMode HPMClassicCAN::ConvertMode(const CAN::Mode& mode)
{
  if (mode.loopback && mode.listen_only)
  {
    return can_mode_loopback_internal;
  }
  if (mode.loopback)
  {
    return can_mode_loopback_internal;
  }
  if (mode.listen_only)
  {
    return can_mode_listen_only;
  }
  return can_mode_normal;
}

ErrorCode HPMClassicCAN::SetConfig(const CAN::Configuration& cfg)
{
  if (can_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  DisableCanInterrupts();

  can_config_t config{};
  if (can_get_default_config(&config) != status_success)
  {
    EnableCanInterrupts();
    return ErrorCode::FAILED;
  }

  config.enable_canfd = false;
  config.mode = ConvertMode(cfg.mode);
  config.disable_ptb_retransmission = cfg.mode.one_shot;
  config.disable_stb_retransmission = cfg.mode.one_shot;
  config.irq_txrx_enable_mask = TX_RX_IRQ_MASK;
  config.irq_error_enable_mask = ERROR_IRQ_MASK;

  const bool use_low_level = cfg.bit_timing.brp != 0u || cfg.bit_timing.prop_seg != 0u ||
                             cfg.bit_timing.phase_seg1 != 0u ||
                             cfg.bit_timing.phase_seg2 != 0u || cfg.bit_timing.sjw != 0u;

  if (use_low_level)
  {
    const uint32_t seg1 = cfg.bit_timing.prop_seg + cfg.bit_timing.phase_seg1;
    if (cfg.bit_timing.brp == 0u || seg1 == 0u || cfg.bit_timing.phase_seg2 == 0u ||
        cfg.bit_timing.sjw == 0u)
    {
      EnableCanInterrupts();
      return ErrorCode::ARG_ERR;
    }

    config.use_lowlevel_timing_setting = true;
    config.can_timing.prescaler = static_cast<uint16_t>(cfg.bit_timing.brp);
    config.can_timing.num_seg1 = static_cast<uint16_t>(seg1);
    config.can_timing.num_seg2 = static_cast<uint16_t>(cfg.bit_timing.phase_seg2);
    config.can_timing.num_sjw = static_cast<uint16_t>(cfg.bit_timing.sjw);
  }
  else if (cfg.bitrate != 0u)
  {
    config.use_lowlevel_timing_setting = false;
    config.baudrate = cfg.bitrate;
    config.can20_samplepoint_min = sample_point_to_hpm_range(cfg.sample_point, false);
    config.can20_samplepoint_max = sample_point_to_hpm_range(cfg.sample_point, true);
  }

  const ErrorCode result = ConvertStatus(can_init(can_, &config, GetClockFreq()));
  if (result == ErrorCode::OK)
  {
    EnableCanInterrupts();
    (void)EnableInterrupt();
  }
  else
  {
    EnableCanInterrupts();
  }
  return result;
}

uint32_t HPMClassicCAN::GetClockFreq() const { return clock_get_frequency(clock_); }

void HPMClassicCAN::BuildTxMessage(const ClassicPack& pack, LibXRHpmClassicCanTxMessage& message)
{
  std::memset(&message, 0, sizeof(message));

  const bool IS_EXT = pack.type == Type::EXTENDED || pack.type == Type::REMOTE_EXTENDED;
  const bool IS_RTR =
      pack.type == Type::REMOTE_STANDARD || pack.type == Type::REMOTE_EXTENDED;

  message.id = IS_EXT ? (pack.id & 0x1FFFFFFFu) : (pack.id & 0x7FFu);
  message.dlc = pack.dlc <= 8u ? pack.dlc : 8u;
  message.canfd_frame = 0u;
  message.bitrate_switch = 0u;
  message.remote_frame = IS_RTR ? 1u : 0u;
  message.extend_id = IS_EXT ? 1u : 0u;

  if (!IS_RTR)
  {
    std::memcpy(message.data, pack.data, message.dlc);
  }
}

void HPMClassicCAN::BuildRxPack(const LibXRHpmClassicCanRxMessage& message, ClassicPack& pack)
{
  const bool IS_EXT = message.extend_id != 0u;
  const bool IS_RTR = message.remote_frame != 0u;

  pack.id = IS_EXT ? (message.id & 0x1FFFFFFFu) : (message.id & 0x7FFu);
  if (IS_RTR)
  {
    pack.type = IS_EXT ? Type::REMOTE_EXTENDED : Type::REMOTE_STANDARD;
  }
  else
  {
    pack.type = IS_EXT ? Type::EXTENDED : Type::STANDARD;
  }

  pack.dlc = message.dlc <= 8u ? message.dlc : 8u;
  std::memset(pack.data, 0, sizeof(pack.data));
  if (!IS_RTR)
  {
    std::memcpy(pack.data, message.data, pack.dlc);
  }
}

CAN::ErrorID HPMClassicCAN::ConvertErrorKind(uint8_t kind)
{
  switch (kind)
  {
    case CAN_KIND_OF_ERROR_BUS_OFF:
      return ErrorID::CAN_ERROR_ID_BUS_OFF;
    case CAN_KIND_OF_ERROR_BIT_ERROR:
      return ErrorID::CAN_ERROR_ID_BIT0;
    case CAN_KIND_OF_ERROR_FORM_ERROR:
      return ErrorID::CAN_ERROR_ID_FORM;
    case CAN_KIND_OF_ERROR_STUFF_ERROR:
      return ErrorID::CAN_ERROR_ID_STUFF;
    case CAN_KIND_OF_ERROR_ACK_ERROR:
      return ErrorID::CAN_ERROR_ID_ACK;
    case CAN_KIND_OF_ERROR_CRC_ERROR:
      return ErrorID::CAN_ERROR_ID_CRC;
    default:
      return ErrorID::CAN_ERROR_ID_OTHER;
  }
}

void HPMClassicCAN::TxService()
{
  if (can_ == nullptr)
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

    while (can_get_secondary_transmit_buffer_status(can_) != CAN_STB_IS_FULL)
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

      LibXRHpmClassicCanTxMessage tx{};
      BuildTxMessage(pack, tx);

      const LibXRHpmClassicCanStatus status = can_send_message_nonblocking(can_, &tx);
      if (status == status_can_tx_fifo_full)
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

ErrorCode HPMClassicCAN::AddMessage(const ClassicPack& pack)
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

void HPMClassicCAN::ProcessRx()
{
  if (can_ == nullptr)
  {
    return;
  }

  while (can_is_data_available_in_receive_buffer(can_))
  {
    LibXRHpmClassicCanRxMessage rx{};
    const LibXRHpmClassicCanStatus status = can_read_received_message(can_, &rx);

    if (status == status_success)
    {
      ClassicPack pack{};
      BuildRxPack(rx, pack);
      OnMessage(pack, true);
      continue;
    }

    ClassicPack error_pack{};
    error_pack.type = Type::ERROR;
    error_pack.id = FromErrorID(ConvertErrorKind(can_get_last_error_kind(can_)));
    OnMessage(error_pack, true);
  }
}

void HPMClassicCAN::ProcessTx() { TxService(); }

void HPMClassicCAN::ProcessError(uint8_t error_flags, uint8_t last_error_kind)
{
  if (error_flags == 0u && last_error_kind == CAN_KIND_OF_ERROR_NO_ERROR)
  {
    return;
  }

  ClassicPack pack{};
  pack.type = Type::ERROR;

  if (can_is_in_bus_off_mode(can_))
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_BUS_OFF);
  }
  else if ((error_flags & CAN_ERROR_PASSIVE_MODE_ACTIVE_FLAG) != 0u ||
           (error_flags & CAN_ERROR_PASSIVE_INT_FLAG) != 0u)
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_ERROR_PASSIVE);
  }
  else if ((error_flags & CAN_ERROR_WARNING_LIMIT_FLAG) != 0u)
  {
    pack.id = FromErrorID(ErrorID::CAN_ERROR_ID_ERROR_WARNING);
  }
  else
  {
    pack.id = FromErrorID(ConvertErrorKind(last_error_kind));
  }

  OnMessage(pack, true);
}

void HPMClassicCAN::ProcessInterrupt()
{
  if (can_ == nullptr)
  {
    return;
  }

  const uint8_t tx_rx_flags = can_get_tx_rx_flags(can_);
  const uint8_t error_flags = can_get_error_interrupt_flags(can_);
  const uint8_t last_error_kind = can_get_last_error_kind(can_);

  if ((tx_rx_flags & (CAN_EVENT_RECEIVE | CAN_EVENT_RX_BUF_OVERRUN)) != 0u)
  {
    ProcessRx();
  }

  if ((tx_rx_flags & CAN_EVENT_TX_SECONDARY_BUF) != 0u)
  {
    ProcessTx();
  }

  if ((tx_rx_flags & CAN_EVENT_ERROR) != 0u || (error_flags & ERROR_FLAG_MASK) != 0u)
  {
    ProcessError(error_flags, last_error_kind);
  }

  if (tx_rx_flags != 0u)
  {
    can_clear_tx_rx_flags(can_, tx_rx_flags);
  }
  if (error_flags != 0u)
  {
    can_clear_error_interrupt_flags(can_, error_flags);
  }
}

void HPMClassicCAN::EnableCanInterrupts()
{
  if (can_ == nullptr)
  {
    return;
  }
  can_enable_tx_rx_irq(can_, TX_RX_IRQ_MASK);
  can_enable_error_irq(can_, ERROR_IRQ_MASK);
}

void HPMClassicCAN::DisableCanInterrupts()
{
  if (can_ == nullptr)
  {
    return;
  }
  can_disable_tx_rx_irq(can_, 0xFFu);
  can_disable_error_irq(can_, 0xFFu);
}

ErrorCode HPMClassicCAN::EnableInterrupt()
{
  if (irq_ == INVALID_IRQ)
  {
    return ErrorCode::OK;
  }

  intc_m_enable_irq_with_priority(irq_, 1);
  return ErrorCode::OK;
}

ErrorCode HPMClassicCAN::DisableInterrupt()
{
  if (irq_ == INVALID_IRQ)
  {
    return ErrorCode::OK;
  }

  intc_m_disable_irq(irq_);
  return ErrorCode::OK;
}

ErrorCode HPMClassicCAN::GetErrorState(CAN::ErrorState& state) const
{
  if (can_ == nullptr)
  {
    return ErrorCode::ARG_ERR;
  }

  state.tx_error_counter = can_get_transmit_error_count(can_);
  state.rx_error_counter = can_get_receive_error_count(can_);
  state.bus_off = can_is_in_bus_off_mode(can_);
  state.error_passive =
      (can_get_error_interrupt_flags(can_) & CAN_ERROR_PASSIVE_MODE_ACTIVE_FLAG) != 0u;
  state.error_warning =
      (can_get_error_interrupt_flags(can_) & CAN_ERROR_WARNING_LIMIT_FLAG) != 0u;

  return ErrorCode::OK;
}

#else

HPMClassicCAN::HPMClassicCAN(CAN_Type* can, clock_name_t clock, uint32_t irq, uint32_t queue_size)
    : can_(can), clock_(clock), irq_(irq), tx_queue_(queue_size)
{
}

ErrorCode HPMClassicCAN::SetConfig(const CAN::Configuration& cfg)
{
  (void)cfg;
  return ErrorCode::NOT_SUPPORT;
}

uint32_t HPMClassicCAN::GetClockFreq() const { return 0; }

ErrorCode HPMClassicCAN::AddMessage(const ClassicPack& pack)
{
  (void)pack;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMClassicCAN::GetErrorState(CAN::ErrorState& state) const
{
  (void)state;
  return ErrorCode::NOT_SUPPORT;
}

void HPMClassicCAN::ProcessInterrupt() {}

ErrorCode HPMClassicCAN::EnableInterrupt() { return ErrorCode::NOT_SUPPORT; }

ErrorCode HPMClassicCAN::DisableInterrupt() { return ErrorCode::NOT_SUPPORT; }

ErrorCode HPMClassicCAN::ConvertStatus(LibXRHpmClassicCanStatus status)
{
  (void)status;
  return ErrorCode::NOT_SUPPORT;
}

LibXRHpmClassicCanMode HPMClassicCAN::ConvertMode(const CAN::Mode& mode)
{
  (void)mode;
  return {};
}

void HPMClassicCAN::BuildTxMessage(const ClassicPack& pack, LibXRHpmClassicCanTxMessage& message)
{
  (void)pack;
  (void)message;
}

void HPMClassicCAN::BuildRxPack(const LibXRHpmClassicCanRxMessage& message, ClassicPack& pack)
{
  (void)message;
  (void)pack;
}

CAN::ErrorID HPMClassicCAN::ConvertErrorKind(uint8_t kind)
{
  (void)kind;
  return ErrorID::CAN_ERROR_ID_GENERIC;
}

void HPMClassicCAN::EnableCanInterrupts() {}
void HPMClassicCAN::DisableCanInterrupts() {}
void HPMClassicCAN::TxService() {}
void HPMClassicCAN::ProcessRx() {}
void HPMClassicCAN::ProcessTx() {}
void HPMClassicCAN::ProcessError(uint8_t error_flags, uint8_t last_error_kind)
{
  (void)error_flags;
  (void)last_error_kind;
}

#endif
