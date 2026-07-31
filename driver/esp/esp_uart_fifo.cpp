#include "esp_uart_fifo.hpp"

#include <algorithm>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_uart_hal.hpp"
#include "soc/soc_caps.h"

namespace
{
constexpr uint8_t RX_TIMEOUT_THRESHOLD = 1U;
constexpr uint32_t RX_DATA_INTR_MASK = UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT;
constexpr uint32_t RX_ERROR_INTR_MASK =
    UART_INTR_PARITY_ERR | UART_INTR_FRAM_ERR | UART_INTR_RXFIFO_OVF;
constexpr uint32_t RX_INTR_MASK = RX_DATA_INTR_MASK | RX_ERROR_INTR_MASK;
constexpr uint32_t TX_SPACE_INTR_MASK = UART_INTR_TXFIFO_EMPTY;
constexpr uint32_t TX_IDLE_INTR_MASK = UART_INTR_TX_DONE;
constexpr uint32_t OWNED_INTR_MASK =
    RX_INTR_MASK | TX_SPACE_INTR_MASK | TX_IDLE_INTR_MASK;
constexpr uint32_t TX_EMPTY_THRESHOLD = SOC_UART_FIFO_LEN / 2U;
constexpr uint32_t RX_FULL_THRESHOLD = SOC_UART_FIFO_LEN / 2U;

}  // namespace

namespace LibXR
{

void Detail::ESP32UartFifoReadPort::OnRxDequeue(bool in_isr) { owner_.ResumeRx(in_isr); }

ErrorCode ESP32UartFifo::InitPowerManagement()
{
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  if (pm_lock_ != nullptr)
  {
    return ErrorCode::STATE_ERR;
  }

  const esp_pm_lock_type_t lock_type = Detail::ESP_UART_CLOCK_REQUIRES_APB_LOCK
                                           ? ESP_PM_APB_FREQ_MAX
                                           : ESP_PM_NO_LIGHT_SLEEP;
  if (esp_pm_lock_create(lock_type, 0, "libxr_uart", &pm_lock_) != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  if (esp_pm_lock_acquire(pm_lock_) != ESP_OK)
  {
    (void)esp_pm_lock_delete(pm_lock_);
    pm_lock_ = nullptr;
    return ErrorCode::INIT_ERR;
  }
#else
  (void)Detail::ESP_UART_CLOCK_REQUIRES_APB_LOCK;
#endif
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32UartFifo::SetIrqDomainEnabled(bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  SetIrqDomainEnabledLocked(enabled);
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void IRAM_ATTR ESP32UartFifo::SetIrqDomainEnabledLocked(bool enabled) noexcept
{
  const bool masked = !enabled;
  if (irq_domain_masked_ == masked)
  {
    return;
  }

  const bool in_isr = xPortInIsrContext() != pdFALSE;
  const bool success = uart_intr_handle_ != nullptr &&
                       (enabled ? esp_intr_enable(uart_intr_handle_)
                                : esp_intr_disable(uart_intr_handle_)) == ESP_OK;
  REQUIRE_FROM_CALLBACK(success, in_isr);
  irq_domain_masked_ = masked;
}

ESP32UartFifo::ESP32UartFifo(uart_port_t uart_num, int tx_pin, int rx_pin, int rts_pin,
                             int cts_pin, size_t rx_buffer_size, size_t tx_buffer_size,
                             uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      uart_num_(uart_num),
      requested_config_(config),
      execution_policy_(*this),
      _read_port(rx_buffer_size, *this),
      _write_port(tx_queue_size, tx_buffer_size)
{
  REQUIRE(!Detail::IsEspConsoleUartInUse(uart_num_));
  REQUIRE(uart_num_ < UART_NUM_MAX);
  REQUIRE(uart_num_ < SOC_UART_HP_NUM);
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_buffer_size > 0U);
  REQUIRE(tx_queue_size > 0U);
  if constexpr (Detail::ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(Detail::IsCurrentTaskPinnedToOneCore());
  }

  _read_port = ReadFun;
  _write_port = WriteFun;

  REQUIRE(InitUartHardware(tx_pin, rx_pin, rts_pin, cts_pin) == ErrorCode::OK);
  REQUIRE(InitPowerManagement() == ErrorCode::OK);
  REQUIRE(InstallUartIsr() == ErrorCode::OK);
  ConfigureRxInterruptPath();
  SetIrqDomainEnabled(true);
}

void IRAM_ATTR ESP32UartFifo::UartIsrEntry(void* arg)
{
  auto* self = static_cast<ESP32UartFifo*>(arg);
  if (self == nullptr)
  {
    return;
  }

  bool pushed_any = false;
  (void)self->execution_policy_.InvokeIrq(
      [self]() noexcept { return self->ServiceIrqSource(true); },
      [self, &pushed_any](uint32_t events) noexcept
      { return self->ServiceEvents(events, true, nullptr, pushed_any); });
  if (pushed_any)
  {
    self->_read_port.ProcessPendingReads(true);
  }
}

ErrorCode ESP32UartFifo::InstallUartIsr()
{
  if (uart_isr_installed_)
  {
    return ErrorCode::OK;
  }

  constexpr int UART_INTR_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;
  const esp_err_t result =
      esp_intr_alloc(Detail::GetEspUartInterruptSource(uart_num_), UART_INTR_FLAGS,
                     UartIsrEntry, this, &uart_intr_handle_);
  if (result != ESP_OK)
  {
    return ErrorCode::INIT_ERR;
  }
  uart_isr_installed_ = true;
  return ErrorCode::OK;
}

ErrorCode ESP32UartFifo::SetConfig(UART::Configuration config)
{
  const ErrorCode validation = ValidateConfig(config);
  if (validation != ErrorCode::OK)
  {
    return validation;
  }
  if (!rx_config_gate_.TryReserveConfig())
  {
    return ErrorCode::BUSY;
  }

  requested_config_ = config;
  rx_config_gate_.PublishConfig();

  bool pushed_any = false;
  (void)execution_policy_.Invoke(
      EventMask(Event::CONFIG),
      [this, &pushed_any](uint32_t events) noexcept
      {
        return ServiceEvents(events, xPortInIsrContext() != pdFALSE, nullptr, pushed_any);
      });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(xPortInIsrContext() != pdFALSE);
  }
  return ErrorCode::OK;
}

ErrorCode ESP32UartFifo::SetLoopback(bool enable)
{
  if (!uart_hw_enabled_)
  {
    return ErrorCode::STATE_ERR;
  }
  Detail::SetEspUartLoopback(uart_hal_, enable);
  return ErrorCode::OK;
}

ErrorCode IRAM_ATTR ESP32UartFifo::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &ESP32UartFifo::_write_port);
  return uart->SubmitWrite(in_isr);
}

ErrorCode ESP32UartFifo::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

ErrorCode IRAM_ATTR ESP32UartFifo::SubmitWrite(bool in_isr)
{
  SubmitContext submit{};
  bool pushed_any = false;
  (void)execution_policy_.Invoke(
      EventMask(Event::WRITE),
      [this, in_isr, &submit, &pushed_any](uint32_t events) noexcept
      { return ServiceEvents(events, in_isr, &submit, pushed_any); });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(in_isr);
  }
  return submit.result_;
}

void IRAM_ATTR ESP32UartFifo::ResumeRx(bool in_isr)
{
  bool pushed_any = false;
  (void)execution_policy_.Invoke(
      EventMask(Event::RX_SPACE), [this, in_isr, &pushed_any](uint32_t events) noexcept
      { return ServiceEvents(events, in_isr, nullptr, pushed_any); });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(in_isr);
  }
}

uint32_t IRAM_ATTR ESP32UartFifo::ServiceIrqSource(bool) noexcept
{
  const uint32_t status = uart_hal_get_intsts_mask(&uart_hal_) & OWNED_INTR_MASK;
  uint32_t events = 0U;
  uint32_t disable_mask = 0U;

  const uint32_t rx_status = status & RX_INTR_MASK;
  const bool tx_space_triggered = (status & TX_SPACE_INTR_MASK) != 0U;
  const bool tx_idle_triggered =
      ((status & TX_IDLE_INTR_MASK) != 0U) && config_tx_idle_interrupt_armed_;
  if (rx_status != 0U)
  {
    disable_mask |= RX_INTR_MASK;
  }
  if (tx_space_triggered)
  {
    disable_mask |= TX_SPACE_INTR_MASK;
  }
  if (tx_idle_triggered)
  {
    disable_mask |= TX_IDLE_INTR_MASK;
  }
  if (disable_mask != 0U)
  {
    SetOwnedInterruptsEnabled(disable_mask, false);
  }

  if (rx_status != 0U)
  {
    // RX conditions can reassert immediately while the service owner is preempted.
    // Make this IRQ a one-shot carrier; ServiceRx() decides whether to re-enable it.
    uart_hal_clr_intsts_mask(&uart_hal_, rx_status);
    if ((rx_status & RX_DATA_INTR_MASK) != 0U)
    {
      events |= EventMask(Event::RX_DATA);
    }
    if ((rx_status & UART_INTR_PARITY_ERR) != 0U)
    {
      events |= EventMask(Event::RX_PARITY_ERROR);
    }
    if ((rx_status & UART_INTR_FRAM_ERR) != 0U)
    {
      events |= EventMask(Event::RX_FRAME_ERROR);
    }
    if ((rx_status & UART_INTR_RXFIFO_OVF) != 0U)
    {
      events |= EventMask(Event::RX_OVERFLOW);
    }
  }

  if (tx_space_triggered)
  {
    // TX_EMPTY is condition-triggered. Leaving it enabled after acknowledgement can
    // starve an interrupted DirectPolicy owner before it consumes TX_SPACE.
    uart_hal_clr_intsts_mask(&uart_hal_, TX_SPACE_INTR_MASK);
    events |= EventMask(Event::TX_SPACE);
  }

  // TX_DONE can precede the FSM's idle indication. Preserve the raw bit, but mask its
  // source and let ContinueConfiguration() either re-arm the check or apply CONFIG.
  if (tx_idle_triggered)
  {
    events |= EventMask(Event::TX_IDLE);
  }
  return events;
}

uint32_t IRAM_ATTR ESP32UartFifo::ServiceEvents(uint32_t events, bool in_isr,
                                                SubmitContext* submit,
                                                bool& pushed_any) noexcept
{
  uint32_t continuation = 0U;

  if ((submit != nullptr) &&
      ((config_state_ != ConfigState::NORMAL) || rx_config_gate_.ConfigRequested()))
  {
    // This Write is only carrying an already accepted CONFIG forward. Its stack-local
    // shortcut must not complete a queued record whose durable operation outlives it.
    submit->synchronous_completion_allowed_ = false;
  }

  // ServiceIrqSource() physically masks condition-triggered carriers but deliberately
  // leaves these logical flags intact until the retained event reaches its owner.
  if ((events & EventMask(Event::TX_SPACE)) != 0U)
  {
    DisarmTxSpaceInterrupt();
  }
  if ((events & EventMask(Event::TX_IDLE)) != 0U)
  {
    config_tx_idle_interrupt_armed_ = false;
  }

  if ((events & EventMask(Event::CONFIG)) != 0U)
  {
    BeginConfiguration();
  }
  if (((events & EventMask(Event::CONTROL_READY)) != 0U) &&
      (config_state_ == ConfigState::NORMAL) && rx_config_gate_.ConfigRequested())
  {
    BeginConfiguration();
  }

  constexpr uint32_t RX_EVENT_MASK =
      EventMask(Event::RX_DATA) | EventMask(Event::RX_SPACE) |
      EventMask(Event::RX_PARITY_ERROR) | EventMask(Event::RX_FRAME_ERROR) |
      EventMask(Event::RX_OVERFLOW);
  if ((events & RX_EVENT_MASK) != 0U)
  {
    continuation |= ServiceRx(events, in_isr, pushed_any);
  }

  if (config_state_ == ConfigState::DRAINING_RECORD)
  {
    if (HasCurrentRecord())
    {
      (void)FillCurrentRecord(in_isr, false, nullptr);
    }
    if (!HasCurrentRecord())
    {
      config_state_ = ConfigState::WAITING_LINE_IDLE;
    }
    continuation |= ContinueConfiguration(in_isr);
  }
  else if (config_state_ == ConfigState::WAITING_LINE_IDLE)
  {
    continuation |= ContinueConfiguration(in_isr);
  }
  else if ((events & (EventMask(Event::WRITE) | EventMask(Event::TX_SPACE))) != 0U)
  {
    ProgressTx(in_isr, submit);
  }

  return continuation;
}

void ESP32UartFifo::BeginConfiguration()
{
  if (config_state_ != ConfigState::NORMAL)
  {
    return;
  }
  if (!rx_config_gate_.TryEnterConfig())
  {
    return;
  }

  SetRxInterruptPathEnabled(false);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, RX_INTR_MASK);

  if (HasCurrentRecord())
  {
    config_state_ = ConfigState::DRAINING_RECORD;
  }
  else
  {
    DisarmTxSpaceInterrupt();
    config_state_ = ConfigState::WAITING_LINE_IDLE;
  }
}

uint32_t ESP32UartFifo::ContinueConfiguration(bool in_isr)
{
  if (config_state_ == ConfigState::DRAINING_RECORD)
  {
    if (HasCurrentRecord())
    {
      return 0U;
    }
    DisarmTxSpaceInterrupt();
    config_state_ = ConfigState::WAITING_LINE_IDLE;
  }
  if (config_state_ != ConfigState::WAITING_LINE_IDLE)
  {
    return 0U;
  }

  if (!uart_hal_is_tx_idle(&uart_hal_))
  {
    ArmConfigTxIdleInterrupt();
    if (!uart_hal_is_tx_idle(&uart_hal_))
    {
      return 0U;
    }
  }

  DisarmConfigTxIdleInterrupt();
  REQUIRE_FROM_CALLBACK(ApplyConfigPayload(requested_config_), in_isr);
  ConfigureRxInterruptPath();
  config_state_ = ConfigState::NORMAL;
  rx_config_gate_.LeaveConfig();
  return EventMask(Event::WRITE) | EventMask(Event::RX_SPACE);
}

uint32_t IRAM_ATTR ESP32UartFifo::ServiceRx(uint32_t events, bool in_isr,
                                            bool& pushed_any)
{
  // RX hardware events are one-shot carriers. Keep the source masked while this owner
  // drains the FIFO, then re-enable it only when the software queue has room.
  SetRxInterruptPathEnabled(false);
  if (config_state_ != ConfigState::NORMAL)
  {
    return 0U;
  }

  if ((events & EventMask(Event::RX_SPACE)) != 0U)
  {
    const uint32_t raw_status = uart_hal_get_intraw_mask(&uart_hal_) & RX_INTR_MASK;
    if (raw_status != 0U)
    {
      uart_hal_clr_intsts_mask(&uart_hal_, raw_status);
      if ((raw_status & RX_DATA_INTR_MASK) != 0U)
      {
        events |= EventMask(Event::RX_DATA);
      }
      if ((raw_status & UART_INTR_PARITY_ERR) != 0U)
      {
        events |= EventMask(Event::RX_PARITY_ERROR);
      }
      if ((raw_status & UART_INTR_FRAM_ERR) != 0U)
      {
        events |= EventMask(Event::RX_FRAME_ERROR);
      }
      if ((raw_status & UART_INTR_RXFIFO_OVF) != 0U)
      {
        events |= EventMask(Event::RX_OVERFLOW);
      }
    }
  }

  if (!rx_config_gate_.TryEnterRx())
  {
    SetRxInterruptPathEnabled(false);
    return 0U;
  }

  constexpr uint32_t RX_ERROR_EVENT_MASK = EventMask(Event::RX_PARITY_ERROR) |
                                           EventMask(Event::RX_FRAME_ERROR) |
                                           EventMask(Event::RX_OVERFLOW);
  if ((events & RX_ERROR_EVENT_MASK) != 0U)
  {
    // ESP UART error status does not identify a trustworthy byte boundary. Drop the
    // hardware FIFO and resume RX locally; independent TX progress is untouched.
    uart_hal_rxfifo_rst(&uart_hal_);
  }

  pushed_any = DrainRxFifo(in_isr) || pushed_any;
  const bool control_ready = rx_config_gate_.LeaveRx();

  uint32_t continuation = control_ready ? EventMask(Event::CONTROL_READY) : 0U;
  if (_read_port.queue_data_->EmptySize() == 0U)
  {
    SetRxInterruptPathEnabled(false);
  }
  else
  {
    SetRxInterruptPathEnabled(true);
    if (uart_hal_get_rxfifo_len(&uart_hal_) != 0U)
    {
      continuation |= EventMask(Event::RX_DATA);
    }
  }
  return continuation;
}

bool IRAM_ATTR ESP32UartFifo::DrainRxFifo(bool in_isr)
{
  bool pushed_any = false;
  while (uart_hal_get_rxfifo_len(&uart_hal_) != 0U)
  {
    const size_t fifo_size = uart_hal_get_rxfifo_len(&uart_hal_);
    const size_t write_size = std::min(fifo_size, _read_port.queue_data_->EmptySize());
    if (write_size == 0U)
    {
      break;
    }

    const ErrorCode result = _read_port.queue_data_->PushWithWriter(
        write_size,
        [this](uint8_t* buffer, size_t size) -> ErrorCode
        {
          int read_size = static_cast<int>(size);
          uart_hal_read_rxfifo(&uart_hal_, buffer, &read_size);
          return read_size == static_cast<int>(size) ? ErrorCode::OK : ErrorCode::EMPTY;
        });
    REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
    pushed_any = true;
  }
  return pushed_any;
}

void IRAM_ATTR ESP32UartFifo::ProgressTx(bool in_isr, SubmitContext* submit)
{
  while (config_state_ == ConfigState::NORMAL)
  {
    if (HasCurrentRecord())
    {
      if (!FillCurrentRecord(in_isr, false, nullptr))
      {
        return;
      }
    }

    bool synchronous_submission = false;
    if (!ClaimNextRecord(in_isr, submit, synchronous_submission))
    {
      DisarmTxSpaceInterrupt();
      return;
    }
    if (!FillCurrentRecord(in_isr, synchronous_submission, submit))
    {
      return;
    }
  }
}

bool IRAM_ATTR ESP32UartFifo::ClaimNextRecord(bool in_isr, SubmitContext* submit,
                                              bool& synchronous_submission)
{
  ASSERT(!HasCurrentRecord());
  if (!rx_config_gate_.TryEnterTx())
  {
    return false;
  }

  WriteInfoBlock info{};
  if (_write_port.queue_info_->Peek(info) != ErrorCode::OK)
  {
    rx_config_gate_.LeaveTx();
    return false;
  }

  REQUIRE_FROM_CALLBACK(_write_port.queue_data_ != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(info.data.size_ > 0U, in_isr);
  REQUIRE_FROM_CALLBACK(info.data.size_ <= _write_port.queue_data_->Size(), in_isr);
  synchronous_submission = submit != nullptr && submit->synchronous_completion_allowed_ &&
                           !submit->resolved_ && _write_port.queue_info_->Size() == 1U;

  const ErrorCode pop_result = _write_port.queue_info_->Pop(info);
  REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);

  current_record_ = info;
  current_record_offset_ = 0U;

  // This starts a new TX_DONE generation. CONFIG never clears a TX_DONE raised after
  // this point until the line-idle predicate also becomes true.
  uart_hal_clr_intsts_mask(&uart_hal_, TX_IDLE_INTR_MASK);
  rx_config_gate_.LeaveTx();
  return true;
}

bool ESP32UartFifo::HasCurrentRecord() const { return current_record_.data.size_ != 0U; }

bool IRAM_ATTR ESP32UartFifo::FillCurrentRecord(bool in_isr, bool synchronous_submission,
                                                SubmitContext* submit)
{
  ASSERT(HasCurrentRecord());
  const size_t record_size = current_record_.data.size_;
  const size_t fifo_space = uart_hal_get_txfifo_len(&uart_hal_);
  if (fifo_space == 0U)
  {
    ArmTxSpaceInterrupt();
    return false;
  }

  // Consume one FIFO-space snapshot per fill turn. Chasing slots that hardware frees
  // while filling one unfinished record can defer retained RX events until its FIFO
  // overflows on a fast single-core target.
  const size_t write_size = std::min(record_size - current_record_offset_, fifo_space);
  const ErrorCode result = _write_port.queue_data_->PopWithReader(
      write_size,
      [this](const uint8_t* buffer, size_t size) -> ErrorCode
      {
        uint32_t written = 0U;
        uart_hal_write_txfifo(&uart_hal_, buffer, static_cast<uint32_t>(size), &written);
        return written == static_cast<uint32_t>(size) ? ErrorCode::OK : ErrorCode::EMPTY;
      });
  REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  current_record_offset_ += write_size;
  if (current_record_offset_ < record_size)
  {
    ArmTxSpaceInterrupt();
    return false;
  }

  DisarmTxSpaceInterrupt();
  CompleteCurrentRecord(in_isr, synchronous_submission, submit);
  return true;
}

void IRAM_ATTR ESP32UartFifo::CompleteCurrentRecord(bool in_isr,
                                                    bool synchronous_submission,
                                                    SubmitContext* submit)
{
  ASSERT(HasCurrentRecord());
  ASSERT(current_record_offset_ == current_record_.data.size_);
  WriteInfoBlock completed = current_record_;
  ClearCurrentRecord();

  if (synchronous_submission)
  {
    ASSERT(submit != nullptr);
    submit->result_ = ErrorCode::OK;
    submit->resolved_ = true;
    return;
  }
  _write_port.Finish(in_isr, ErrorCode::OK, completed);
}

void ESP32UartFifo::ClearCurrentRecord()
{
  current_record_ = {};
  current_record_offset_ = 0U;
}

void ESP32UartFifo::SetRxInterruptPathEnabled(bool enabled)
{
  if (rx_interrupt_path_enabled_ == enabled)
  {
    return;
  }
  SetOwnedInterruptsEnabled(RX_INTR_MASK, enabled);
  rx_interrupt_path_enabled_ = enabled;
}

void IRAM_ATTR ESP32UartFifo::SetOwnedInterruptsEnabled(uint32_t mask,
                                                        bool enabled) noexcept
{
  // UART interrupt enables share one read-modify-write register. Serialize every
  // owned-bit update so an ISR cannot overwrite a task-side update to another bit.
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  if (enabled)
  {
    uart_hal_ena_intr_mask(&uart_hal_, mask);
  }
  else
  {
    uart_hal_disable_intr_mask(&uart_hal_, mask);
  }
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void ESP32UartFifo::ConfigureRxInterruptPath()
{
  uart_hal_set_rxfifo_full_thr(&uart_hal_, RX_FULL_THRESHOLD);
  uart_hal_set_rx_timeout(&uart_hal_, RX_TIMEOUT_THRESHOLD);
  uart_hal_clr_intsts_mask(&uart_hal_, RX_INTR_MASK);
  SetRxInterruptPathEnabled(true);
}

void ESP32UartFifo::ArmTxSpaceInterrupt()
{
  if (tx_space_interrupt_armed_)
  {
    return;
  }
  uart_hal_clr_intsts_mask(&uart_hal_, TX_SPACE_INTR_MASK);
  tx_space_interrupt_armed_ = true;
  SetOwnedInterruptsEnabled(TX_SPACE_INTR_MASK, true);
}

void ESP32UartFifo::DisarmTxSpaceInterrupt()
{
  if (!tx_space_interrupt_armed_)
  {
    return;
  }
  SetOwnedInterruptsEnabled(TX_SPACE_INTR_MASK, false);
  uart_hal_clr_intsts_mask(&uart_hal_, TX_SPACE_INTR_MASK);
  tx_space_interrupt_armed_ = false;
}

void ESP32UartFifo::ArmConfigTxIdleInterrupt()
{
  if (config_tx_idle_interrupt_armed_)
  {
    return;
  }
  config_tx_idle_interrupt_armed_ = true;
  SetOwnedInterruptsEnabled(TX_IDLE_INTR_MASK, true);
}

void ESP32UartFifo::DisarmConfigTxIdleInterrupt()
{
  if (!config_tx_idle_interrupt_armed_)
  {
    return;
  }
  SetOwnedInterruptsEnabled(TX_IDLE_INTR_MASK, false);
  uart_hal_clr_intsts_mask(&uart_hal_, TX_IDLE_INTR_MASK);
  config_tx_idle_interrupt_armed_ = false;
}

ErrorCode ESP32UartFifo::ValidateConfig(UART::Configuration config) const
{
  return Detail::ValidateEspUartConfig(config, uart_sclk_hz_, uart_hw_enabled_);
}

bool ESP32UartFifo::ApplyConfigPayload(UART::Configuration config)
{
  if (!Detail::ApplyEspUartConfig(uart_hal_, config, uart_sclk_hz_))
  {
    return false;
  }

  uart_hal_set_txfifo_empty_thr(&uart_hal_, TX_EMPTY_THRESHOLD);
  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);

  return true;
}

ErrorCode ESP32UartFifo::InitUartHardware(int tx_pin, int rx_pin, int rts_pin,
                                          int cts_pin)
{
  const ErrorCode init_result =
      Detail::InitEspUartHal(uart_num_, uart_hal_, uart_sclk_hz_);
  if (init_result != ErrorCode::OK)
  {
    return init_result;
  }

  if (!ApplyConfigPayload(requested_config_))
  {
    return ErrorCode::INIT_ERR;
  }
  uart_hw_enabled_ = true;

  if (Detail::ConfigureEspUartPins(uart_num_, tx_pin, rx_pin, rts_pin, cts_pin) !=
      ErrorCode::OK)
  {
    uart_hw_enabled_ = false;
    return ErrorCode::INIT_ERR;
  }

  uart_hal_txfifo_rst(&uart_hal_);
  uart_hal_rxfifo_rst(&uart_hal_);
  uart_hal_clr_intsts_mask(&uart_hal_, UINT32_MAX);
  uart_hal_disable_intr_mask(&uart_hal_, UINT32_MAX);
  rx_interrupt_path_enabled_ = false;
  tx_space_interrupt_armed_ = false;
  config_tx_idle_interrupt_armed_ = false;
  return ErrorCode::OK;
}

}  // namespace LibXR
