#include "mspm0_uart.hpp"

#include <atomic>

using namespace LibXR;

MSPM0UART* MSPM0UART::instance_map_[MAX_UART_INSTANCES] = {nullptr};

static constexpr uint32_t MSPM0_UART_RX_ERROR_INTERRUPT_MASK =
    DL_UART_INTERRUPT_OVERRUN_ERROR | DL_UART_INTERRUPT_BREAK_ERROR |
    DL_UART_INTERRUPT_PARITY_ERROR | DL_UART_INTERRUPT_FRAMING_ERROR |
    DL_UART_INTERRUPT_NOISE_ERROR;

static constexpr uint32_t MSPM0_UART_RX_INTERRUPT_MASK =
    DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_ADDRESS_MATCH;

static constexpr uint32_t MSPM0_UART_BASE_INTERRUPT_MASK =
    MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

MSPM0UART::MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size,
                     uint32_t tx_buffer_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      _read_port(rx_stage_buffer.size_),
      _write_port(tx_queue_size, tx_buffer_size),
      res_(res),
      requested_config_(config)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.clock_freq > 0);
  ASSERT(rx_stage_buffer.addr_ != nullptr);
  ASSERT(rx_stage_buffer.size_ > 0);
  ASSERT(tx_queue_size > 0);
  ASSERT(tx_buffer_size > 0);

  _write_port = WriteFun;

  REQUIRE(res_.index < MAX_UART_INSTANCES);
  REQUIRE(res_.index == ResolveIndex(res_.irqn));
  REQUIRE(instance_map_[res_.index] == nullptr);

  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);

  REQUIRE(ValidateConfig(config) == ErrorCode::OK);
  rx_timeout_mode_ = ResolveRxTimeoutMode();
  ApplyInitialConfig(config);

  instance_map_[res_.index] = this;
  NVIC_ClearPendingIRQ(res_.irqn);
  NVIC_EnableIRQ(res_.irqn);
}

UART::Configuration MSPM0UART::BuildConfigFromSysCfg(UART_Regs* instance,
                                                     uint32_t baudrate)
{
  ASSERT(instance != nullptr);
  ASSERT(baudrate > 0U);

  UART::Configuration config = {baudrate, UART::Parity::NO_PARITY, 8U, 1U};

  switch (DL_UART_getWordLength(instance))
  {
    case DL_UART_WORD_LENGTH_5_BITS:
      config.data_bits = 5U;
      break;
    case DL_UART_WORD_LENGTH_6_BITS:
      config.data_bits = 6U;
      break;
    case DL_UART_WORD_LENGTH_7_BITS:
      config.data_bits = 7U;
      break;
    case DL_UART_WORD_LENGTH_8_BITS:
    default:
      config.data_bits = 8U;
      break;
  }

  switch (DL_UART_getParityMode(instance))
  {
    case DL_UART_PARITY_NONE:
      config.parity = UART::Parity::NO_PARITY;
      break;
    case DL_UART_PARITY_EVEN:
      config.parity = UART::Parity::EVEN;
      break;
    case DL_UART_PARITY_ODD:
      config.parity = UART::Parity::ODD;
      break;
    default:
      // LibXR UART config only supports none/even/odd parity.
      ASSERT(false);
      config.parity = UART::Parity::NO_PARITY;
      break;
  }

  config.stop_bits = (DL_UART_getStopBits(instance) == DL_UART_STOP_BITS_TWO) ? 2U : 1U;
  return config;
}

ErrorCode MSPM0UART::SetConfig(UART::Configuration config)
{
  const ErrorCode validation = ValidateConfig(config);
  if (validation != ErrorCode::OK)
  {
    return validation;
  }
  if (!TryReserveConfig())
  {
    return ErrorCode::BUSY;
  }

  requested_config_ = config;
  PublishConfig();
  Notify(Event::CONFIG);
  return ErrorCode::OK;
}

ErrorCode MSPM0UART::ValidateConfig(UART::Configuration config) const
{
  if (config.baudrate == 0U || config.data_bits < 5U || config.data_bits > 8U ||
      (config.stop_bits != 1U && config.stop_bits != 2U))
  {
    return ErrorCode::ARG_ERR;
  }

  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
    case UART::Parity::EVEN:
    case UART::Parity::ODD:
      return ErrorCode::OK;
    default:
      return ErrorCode::ARG_ERR;
  }
}

bool MSPM0UART::TryReserveConfig()
{
  // Acquire pairs with the previous owner's final IDLE release before this caller
  // overwrites the two-word snapshot.
  uint32_t expected = static_cast<uint32_t>(ConfigAdmission::IDLE);
  return config_admission_.compare_exchange_strong(
      expected, static_cast<uint32_t>(ConfigAdmission::RESERVED),
      std::memory_order_acquire, std::memory_order_relaxed);
}

void MSPM0UART::PublishConfig()
{
  const uint32_t reserved = static_cast<uint32_t>(ConfigAdmission::RESERVED);
  ASSERT(config_admission_.load(std::memory_order_relaxed) == reserved);
  // The owner may read requested_config_ only after acquiring PENDING.
  config_admission_.store(static_cast<uint32_t>(ConfigAdmission::PENDING),
                          std::memory_order_release);
}

bool MSPM0UART::ConfigPublished() const
{
  return config_admission_.load(std::memory_order_acquire) ==
         static_cast<uint32_t>(ConfigAdmission::PENDING);
}

bool MSPM0UART::ConfigRequested() const
{
  return config_admission_.load(std::memory_order_acquire) !=
         static_cast<uint32_t>(ConfigAdmission::IDLE);
}

void MSPM0UART::CompleteConfig()
{
  ASSERT(config_admission_.load(std::memory_order_relaxed) ==
         static_cast<uint32_t>(ConfigAdmission::PENDING));
  config_admission_.store(static_cast<uint32_t>(ConfigAdmission::IDLE),
                          std::memory_order_release);
}

void MSPM0UART::ApplyInitialConfig(UART::Configuration config)
{
  // Construction is the only synchronous quiescent boundary. Runtime CONFIG never
  // calls the SDK helper because it busy-waits after disabling the UART.
  DL_UART_changeConfig(res_.instance);
  ApplyDisabledConfig(config);
}

void MSPM0UART::ApplyDisabledConfig(UART::Configuration config)
{
  DL_UART_WORD_LENGTH word_length = DL_UART_WORD_LENGTH_8_BITS;
  switch (config.data_bits)
  {
    case 5U:
      word_length = DL_UART_WORD_LENGTH_5_BITS;
      break;
    case 6U:
      word_length = DL_UART_WORD_LENGTH_6_BITS;
      break;
    case 7U:
      word_length = DL_UART_WORD_LENGTH_7_BITS;
      break;
    case 8U:
      word_length = DL_UART_WORD_LENGTH_8_BITS;
      break;
    default:
      ASSERT(false);
      break;
  }

  DL_UART_PARITY parity = DL_UART_PARITY_NONE;
  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      parity = DL_UART_PARITY_NONE;
      break;
    case UART::Parity::EVEN:
      parity = DL_UART_PARITY_EVEN;
      break;
    case UART::Parity::ODD:
      parity = DL_UART_PARITY_ODD;
      break;
    default:
      ASSERT(false);
      break;
  }

  const DL_UART_STOP_BITS stop_bits =
      config.stop_bits == 2U ? DL_UART_STOP_BITS_TWO : DL_UART_STOP_BITS_ONE;

  DL_UART_setWordLength(res_.instance, word_length);
  DL_UART_setParityMode(res_.instance, parity);
  DL_UART_setStopBits(res_.instance, stop_bits);

  DL_UART_enableFIFOs(res_.instance);
  DL_UART_setTXFIFOThreshold(res_.instance, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

  DL_UART_configBaudRate(res_.instance, res_.clock_freq, config.baudrate);

  ApplyRxTimeoutMode();

  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFF);
  DL_UART_disableInterrupt(res_.instance, 0xFFFFFFFF);
  rx_timeout_interrupt_armed_ = false;
  rx_interrupt_path_enabled_ = false;
  ConfigureRxInterruptPath();
  tx_interrupt_armed_ = false;
  config_eot_interrupt_armed_ = false;

  DL_UART_enable(res_.instance);
}

ErrorCode MSPM0UART::WriteFun(WritePort& port, bool)
{
  auto* uart = LibXR::ContainerOf(&port, &MSPM0UART::_write_port);
  uart->Notify(Event::WRITE);
  return ErrorCode::PENDING;
}

MSPM0UART::RxTimeoutMode MSPM0UART::ResolveRxTimeoutMode() const
{
  // 分发规则 / Dispatch rule:
  // 1) [LIN路径 / LIN path] UART0 且存在 LIN compare 宏配置 -> LIN_COMPARE
  // 2) [LIN路径 / LIN path] 运行时探测到 LIN counter+compare 已启用 -> LIN_COMPARE
  // 3) [BYTE路径 / BYTE path] 其他情况 -> BYTE_INTERRUPT
#if defined(UART_0_INST) && defined(UART_0_COUNTER_COMPARE_VALUE)
  if (res_.instance == UART_0_INST)
  {
    // 本项目 UART0 在 SysConfig 中配置为 LIN 扩展实例 / UART0 is configured as a
    // LIN extend instance by SysConfig in this project.
    // [LIN路径 / LIN path] 固定走 LIN compare / Force LIN compare path.
    return RxTimeoutMode::LIN_COMPARE;
  }
#endif

  if (DL_UART_isLINCounterEnabled(res_.instance) &&
      DL_UART_isLINCounterCompareMatchEnabled(res_.instance))
  {
    // [LIN路径 / LIN path] 非 UART0 按寄存器能力探测：若 LIN counter+compare
    // 已启用则走 LIN /
    // For non-UART0, use LIN path when LIN counter+compare are already enabled.
    return RxTimeoutMode::LIN_COMPARE;
  }

  // [BYTE路径 / BYTE path] 不满足 LIN 条件时回落到按字节中断路径 /
  // Fall back to byte-interrupt path when LIN conditions are not met.
  return RxTimeoutMode::BYTE_INTERRUPT;
}

uint32_t MSPM0UART::GetTimeoutInterruptMask() const
{
  switch (rx_timeout_mode_)
  {
    // [LIN路径 / LIN path] 使用 LINC0 compare match 作为帧间超时事件 /
    // Use LINC0 compare match as frame-gap timeout event.
    case RxTimeoutMode::LIN_COMPARE:
      return DL_UART_INTERRUPT_LINC0_MATCH;
    // [BYTE路径 / BYTE path] 不使用硬件超时中断 /
    // No hardware timeout interrupt in byte-interrupt mode.
    case RxTimeoutMode::BYTE_INTERRUPT:
    default:
      return 0;
  }
}

uint32_t MSPM0UART::GetTimeoutInterruptEnabledMask() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetTimeoutInterruptMaskedStatus() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getEnabledInterruptStatus(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetTimeoutInterruptRawStatus() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetRxInterruptTimeoutValue() const
{
  return DL_UART_getRXInterruptTimeout(res_.instance);
}

uint32_t MSPM0UART::GetRxFifoThresholdValue() const
{
  return static_cast<uint32_t>(DL_UART_getRXFIFOThreshold(res_.instance));
}

void MSPM0UART::ResetLinCounter()
{
  if (rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    DL_UART_setLINCounterValue(res_.instance, 0);
  }
}

void MSPM0UART::ApplyRxTimeoutMode()
{
  // 基础 UART 配置对 LIN/BYTE 两条路径一致，先统一配置后再处理模式差异 /
  // Apply shared UART settings first, then patch mode-specific differences.
  DL_UART_setCommunicationMode(res_.instance, DL_UART_MODE_NORMAL);
  DL_UART_setAddressMask(res_.instance, 0U);
  DL_UART_setAddress(res_.instance, 0U);
  DL_UART_setRXFIFOThreshold(res_.instance, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
  DL_UART_setRXInterruptTimeout(res_.instance, 0U);

  switch (rx_timeout_mode_)
  {
    // [LIN路径 / LIN path] 使能 LIN counter/compare，超时事件来自 LINC0_MATCH。
    // [LIN路径 / LIN path] Enable LIN counter/compare; timeout event is LINC0_MATCH.
    case RxTimeoutMode::LIN_COMPARE:
      if (!DL_UART_isLINCounterEnabled(res_.instance))
      {
        DL_UART_enableLINCounter(res_.instance);
      }
      if (!DL_UART_isLINCounterCompareMatchEnabled(res_.instance))
      {
        DL_UART_enableLINCounterCompareMatch(res_.instance);
      }
#if defined(UART_0_COUNTER_COMPARE_VALUE)
      DL_UART_setLINCounterCompareValue(res_.instance, UART_0_COUNTER_COMPARE_VALUE);
#endif
      DL_UART_disableLINCountWhileLow(res_.instance);
      ResetLinCounter();
      break;

    // [BYTE路径 / BYTE path] 仅保留按字节 RX 中断，不依赖超时中断。
    // [BYTE路径 / BYTE path] Keep plain per-byte RX interrupt; no timeout IRQ.
    case RxTimeoutMode::BYTE_INTERRUPT:
    default:
      break;
  }
}

void MSPM0UART::OnInterrupt(uint8_t index)
{
  if (index >= MAX_UART_INSTANCES)
  {
    return;
  }

  auto* uart = instance_map_[index];
  if (uart == nullptr)
  {
    return;
  }

  uart->HandleInterrupt();
}

void MSPM0UART::Notify(Event event) noexcept
{
  pending_events_.fetch_or(EventMask(event), std::memory_order_release);
  __DMB();
  NVIC_SetPendingIRQ(res_.irqn);
}

uint32_t MSPM0UART::CaptureIrqEvents() noexcept
{
  const uint32_t timeout_mask = GetTimeoutInterruptMask();
  const uint32_t owned_mask = MSPM0_UART_BASE_INTERRUPT_MASK | DL_UART_INTERRUPT_TX |
                              DL_UART_INTERRUPT_EOT_DONE | timeout_mask;
  uint32_t pending = DL_UART_getEnabledInterruptStatus(res_.instance, owned_mask);

  if (timeout_mask != 0U &&
      DL_UART_getEnabledInterrupts(res_.instance, timeout_mask) != 0U)
  {
    pending |= DL_UART_getRawInterruptStatus(res_.instance, timeout_mask);
  }

  uint32_t events = 0U;
  if ((pending & MSPM0_UART_RX_INTERRUPT_MASK) != 0U)
  {
    SetRxInterruptPathEnabled(false);
    DL_UART_clearInterruptStatus(res_.instance, pending & MSPM0_UART_RX_INTERRUPT_MASK);
    events |= EventMask(Event::RX_DATA);
  }
  if (timeout_mask != 0U && (pending & timeout_mask) != 0U)
  {
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);
    rx_timeout_interrupt_armed_ = false;
    events |= EventMask(Event::RX_TIMEOUT);
  }
  if ((pending & MSPM0_UART_RX_ERROR_INTERRUPT_MASK) != 0U)
  {
    DL_UART_clearInterruptStatus(res_.instance,
                                 pending & MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
    events |= EventMask(Event::ERROR);
  }
  if ((pending & DL_UART_INTERRUPT_TX) != 0U)
  {
    DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_TX);
    tx_interrupt_armed_ = false;
    events |= EventMask(Event::TX_SPACE);
  }
  if ((pending & DL_UART_INTERRUPT_EOT_DONE) != 0U)
  {
    DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
    config_eot_interrupt_armed_ = false;
    events |= EventMask(Event::TX_EOT);
  }
  return events;
}

uint32_t MSPM0UART::ServiceEvents(uint32_t events, bool& pushed_any) noexcept
{
  if ((events & EventMask(Event::RX_TIMEOUT)) != 0U)
  {
    rx_timeout_count_.fetch_add(1U, std::memory_order_relaxed);
  }

  if ((events & EventMask(Event::CONFIG)) != 0U ||
      (config_state_ == ConfigState::NORMAL && ConfigRequested()))
  {
    BeginConfiguration();
  }

  constexpr uint32_t RX_EVENTS =
      EventMask(Event::RX_DATA) | EventMask(Event::RX_TIMEOUT) | EventMask(Event::ERROR);
  if (config_state_ == ConfigState::NORMAL && (events & RX_EVENTS) != 0U)
  {
    events |= ServiceRx(events, pushed_any);
  }

  if (config_state_ == ConfigState::NORMAL && (events & EventMask(Event::CONFIG)) != 0U)
  {
    BeginConfiguration();
  }

  if (config_state_ != ConfigState::NORMAL)
  {
    return ContinueConfiguration(events);
  }
  if ((events & (EventMask(Event::WRITE) | EventMask(Event::TX_SPACE))) != 0U)
  {
    ProgressTx();
  }
  return 0U;
}

void MSPM0UART::HandleInterrupt()
{
  bool pushed_any = false;
  uint32_t events =
      pending_events_.exchange(0U, std::memory_order_acquire) | CaptureIrqEvents();
  while (events != 0U)
  {
    events = ServiceEvents(events, pushed_any);
    events |= pending_events_.exchange(0U, std::memory_order_acquire);
  }

  if (pushed_any)
  {
    // Hardware progression is complete before user callbacks run. A callback may submit
    // another operation, but cannot reenter this owner's current MMIO pass.
    read_port_->ProcessPendingReads(true);
  }
}

void MSPM0UART::BeginConfiguration()
{
  if (config_state_ != ConfigState::NORMAL || !ConfigPublished())
  {
    return;
  }

  SetRxInterruptPathEnabled(false);
  PauseRxTimeout();

  config_state_ = ConfigState::TX_DRAIN;
  if (!HasActiveRecord())
  {
    DisarmTxSpaceInterrupt();
  }
}

uint32_t MSPM0UART::ContinueConfiguration(uint32_t events)
{
  if (config_state_ == ConfigState::TX_DRAIN)
  {
    if (HasActiveRecord() && !FillCurrentRecord())
    {
      return 0U;
    }
    DisarmTxSpaceInterrupt();
    if (tx_line_active_)
    {
      if ((events & EventMask(Event::TX_EOT)) == 0U)
      {
        ArmConfigEotInterrupt();
        if ((DL_UART_getRawInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE) &
             DL_UART_INTERRUPT_EOT_DONE) == 0U)
        {
          return 0U;
        }
      }
      DisarmConfigEotInterrupt();
      tx_line_active_ = false;
    }

    DL_UART_disable(res_.instance);
    config_state_ = ConfigState::WAITING_DISABLED_IDLE;
  }

  if (config_state_ != ConfigState::WAITING_DISABLED_IDLE)
  {
    return 0U;
  }

  // Once disabled, no new character may start. Drain old FIFO bytes, arm RX as the
  // completion carrier for a character already in the shift register, then recheck BUSY.
  DiscardRxFIFO();
  DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
  SetRxInterruptPathEnabled(true);
  if (DL_UART_isBusy(res_.instance))
  {
    return 0U;
  }

  SetRxInterruptPathEnabled(false);
  DiscardRxFIFO();
  DL_UART_disableFIFOs(res_.instance);
  ApplyDisabledConfig(requested_config_);

  config_state_ = ConfigState::NORMAL;
  // Reopen CONFIG admission last. After this release, a higher-priority ISR may
  // immediately reserve and overwrite requested_config_.
  CompleteConfig();
  return EventMask(Event::WRITE);
}

void MSPM0UART::PauseRxTimeout()
{
  const uint32_t timeout_mask = GetTimeoutInterruptMask();
  if (timeout_mask == 0U || !rx_timeout_interrupt_armed_)
  {
    rx_timeout_interrupt_armed_ = false;
    return;
  }

  // The IRQ owner may observe CONFIG after its one hardware-status capture. Mask first,
  // then preserve a timeout that became raw before this configuration pause.
  DL_UART_disableInterrupt(res_.instance, timeout_mask);
  const bool expired =
      (DL_UART_getRawInterruptStatus(res_.instance, timeout_mask) & timeout_mask) != 0U;
  DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
  rx_timeout_interrupt_armed_ = false;
  if (expired)
  {
    rx_timeout_count_.fetch_add(1U, std::memory_order_relaxed);
  }
}

void MSPM0UART::ArmRxTimeout()
{
  const uint32_t timeout_mask = GetTimeoutInterruptMask();
  if (timeout_mask == 0U)
  {
    rx_timeout_interrupt_armed_ = false;
    return;
  }

  ResetLinCounter();
  DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
  DL_UART_enableInterrupt(res_.instance, timeout_mask);
  rx_timeout_interrupt_armed_ = true;
}

uint32_t MSPM0UART::ServiceRx(uint32_t events, bool& pushed_any)
{
  bool received = false;
  bool pushed = false;
  DrainRxFIFO(received, pushed);
  pushed_any = pushed_any || pushed;

  if (received && rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    if (rx_timeout_interrupt_armed_)
    {
      ResetLinCounter();
    }
    else
    {
      // The physical timeout describes a gap after received data, not a ReadPort
      // generation. The UART IRQ owner is the only path that arms or resets it.
      ArmRxTimeout();
    }
  }

  if (!ConfigRequested() && config_state_ == ConfigState::NORMAL)
  {
    SetRxInterruptPathEnabled(true);
  }

  return ConfigRequested() ? EventMask(Event::CONFIG) : 0U;
}

void MSPM0UART::DrainRxFIFO(bool& received, bool& pushed)
{
  while (!DL_UART_isRXFIFOEmpty(res_.instance))
  {
    const uint8_t RX_BYTE = DL_UART_receiveData(res_.instance);
    received = true;

    if (read_port_->queue_data_->Push(RX_BYTE) == ErrorCode::OK)
    {
      pushed = true;
    }
    else
    {
      rx_drop_count_.fetch_add(1U, std::memory_order_relaxed);
    }
  }
}

void MSPM0UART::DiscardRxFIFO()
{
  while (!DL_UART_isRXFIFOEmpty(res_.instance))
  {
    (void)DL_UART_receiveData(res_.instance);
    rx_drop_count_.fetch_add(1U, std::memory_order_relaxed);
  }
}

void MSPM0UART::ProgressTx()
{
  while (config_state_ == ConfigState::NORMAL)
  {
    if (!HasCurrentRecord() && !ClaimNextRecord())
    {
      DisarmTxSpaceInterrupt();
      return;
    }
    if (!TryCommitCurrentRecord())
    {
      DisarmTxSpaceInterrupt();
      return;
    }
    if (!FillCurrentRecord())
    {
      return;
    }
  }
}

bool MSPM0UART::ClaimNextRecord()
{
  ASSERT(!HasCurrentRecord());
  if (ConfigRequested())
  {
    return false;
  }

  WriteInfoBlock info{};
  if (write_port_->queue_info_->Pop(info) != ErrorCode::OK)
  {
    return false;
  }

  REQUIRE_FROM_CALLBACK(info.data.size_ > 0U, true);
  REQUIRE_FROM_CALLBACK(write_port_->queue_data_ != nullptr, true);
  REQUIRE_FROM_CALLBACK(info.data.size_ <= write_port_->queue_data_->Size(), true);
  tx_record_info_ = info;
  tx_record_remaining_ = info.data.size_;
  tx_record_state_ = TxRecordState::HELD;
  return true;
}

bool MSPM0UART::TryCommitCurrentRecord()
{
  if (HasActiveRecord())
  {
    return true;
  }

  ASSERT(tx_record_state_ == TxRecordState::HELD);
  // This post-Pop acquire load is the old/new framing boundary. CONFIG admitted after
  // it waits behind this record; CONFIG already reserved keeps the record HELD.
  if (ConfigRequested())
  {
    return false;
  }
  tx_record_state_ = TxRecordState::ACTIVE;
  return true;
}

bool MSPM0UART::FillCurrentRecord()
{
  ASSERT(HasActiveRecord());
  bool wrote_any = false;
  while (tx_record_remaining_ > 0U && !DL_UART_isTXFIFOFull(res_.instance))
  {
    uint8_t tx_byte = 0U;
    if (write_port_->queue_data_->Pop(tx_byte) != ErrorCode::OK)
    {
      CompleteCurrentRecord(ErrorCode::FAILED);
      DisarmTxSpaceInterrupt();
      return true;
    }

    DL_UART_transmitData(res_.instance, tx_byte);
    if (!wrote_any)
    {
      // Publish the first new byte before clearing EOT so an older serializer cannot
      // reassert its completion between the clear and this refill.
      DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
      tx_line_active_ = true;
    }
    tx_record_remaining_--;
    wrote_any = true;
  }

  if (tx_record_remaining_ != 0U)
  {
    ArmTxSpaceInterrupt();
    return false;
  }

  DisarmTxSpaceInterrupt();
  CompleteCurrentRecord(ErrorCode::OK);
  return true;
}

bool MSPM0UART::HasCurrentRecord() const
{
  return tx_record_state_ != TxRecordState::EMPTY;
}

bool MSPM0UART::HasActiveRecord() const
{
  return tx_record_state_ == TxRecordState::ACTIVE;
}

void MSPM0UART::CompleteCurrentRecord(ErrorCode result)
{
  ASSERT(HasActiveRecord());
  WriteInfoBlock completed = tx_record_info_;
  ClearCurrentRecord();
  write_port_->Finish(true, result, completed);
}

void MSPM0UART::ClearCurrentRecord()
{
  tx_record_info_ = {};
  tx_record_remaining_ = 0U;
  tx_record_state_ = TxRecordState::EMPTY;
}

void MSPM0UART::ConfigureRxInterruptPath()
{
  DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_BASE_INTERRUPT_MASK);
  DL_UART_enableInterrupt(res_.instance, MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
  rx_interrupt_path_enabled_ = false;
  SetRxInterruptPathEnabled(true);
}

void MSPM0UART::SetRxInterruptPathEnabled(bool enabled)
{
  if (rx_interrupt_path_enabled_ == enabled)
  {
    return;
  }
  if (enabled)
  {
    DL_UART_enableInterrupt(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
  }
  else
  {
    DL_UART_disableInterrupt(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
  }
  rx_interrupt_path_enabled_ = enabled;
}

void MSPM0UART::ArmTxSpaceInterrupt()
{
  if (tx_interrupt_armed_)
  {
    return;
  }
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_TX);
  tx_interrupt_armed_ = true;
  DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
  if (!DL_UART_isTXFIFOFull(res_.instance))
  {
    res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
  }
}

void MSPM0UART::DisarmTxSpaceInterrupt()
{
  if (!tx_interrupt_armed_)
  {
    return;
  }
  DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_TX);
  tx_interrupt_armed_ = false;
}

void MSPM0UART::ArmConfigEotInterrupt()
{
  if (config_eot_interrupt_armed_)
  {
    return;
  }
  config_eot_interrupt_armed_ = true;
  DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
}

void MSPM0UART::DisarmConfigEotInterrupt()
{
  if (config_eot_interrupt_armed_)
  {
    DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
  }
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
  config_eot_interrupt_armed_ = false;
}

#if defined(UART0_BASE)
extern "C" void UART0_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(0);
}
#endif

#if defined(UART1_BASE)
extern "C" void UART1_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(1);
}
#endif

#if defined(UART2_BASE)
extern "C" void UART2_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(2);
}
#endif

#if defined(UART3_BASE)
extern "C" void UART3_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(3);
}
#endif

#if defined(UART4_BASE)
extern "C" void UART4_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(4); }
#endif

#if defined(UART5_BASE)
extern "C" void UART5_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(5); }
#endif

#if defined(UART6_BASE)
extern "C" void UART6_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(6); }
#endif

#if defined(UART7_BASE)
extern "C" void UART7_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(7); }
#endif
