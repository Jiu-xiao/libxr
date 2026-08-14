#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>

#include "esp_uart_hal.hpp"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
constexpr uint32_t TX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
constexpr uint32_t RX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;
constexpr uint32_t ALL_INTR_MASK = TX_INTR_MASK | RX_INTR_MASK;
constexpr size_t ENDPOINT_SIZE = 64U;
}  // namespace

namespace LibXR
{

void ESP32CDCJtagReadPort::OnRxDequeue(bool in_isr) { owner_.ResumeRx(in_isr); }

ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      config_(config),
      execution_policy_(*this),
      _read_port(rx_buffer_size, *this),
      _write_port(tx_queue_size, tx_buffer_size)
{
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_buffer_size > 0U);
  REQUIRE(tx_queue_size > 0U);
  if constexpr (Detail::ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(Detail::IsCurrentTaskPinnedToOneCore());
  }

  _write_port = WriteFun;
  REQUIRE(SetConfig(config_) == ErrorCode::OK);
  REQUIRE(InitHardware() == ErrorCode::OK);
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config)
{
  if ((config.data_bits != 8) || (config.stop_bits != 1) ||
      (config.parity != UART::Parity::NO_PARITY))
  {
    return ErrorCode::ARG_ERR;
  }
  config_ = config;
  return ErrorCode::OK;
}

ErrorCode ESP32CDCJtag::InitHardware()
{
  if (hw_inited_)
  {
    return ErrorCode::OK;
  }

  constexpr int INTR_FLAGS = ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_INTRDISABLED;
  const esp_err_t result = esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, INTR_FLAGS,
                                          IsrEntry, this, &intr_handle_);
  if (result != ESP_OK)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }
  usb_serial_jtag_ll_disable_intr_mask(ALL_INTR_MASK);
  // Preserve pre-existing FIFO status: RX may already contain a host packet, and TX
  // empty must remain available as the first one-shot carrier after it is armed.
  // 保留已有 FIFO 状态：RX 中可能已有主机数据包，TX empty 也必须能在首次 arm 后作为
  // one-shot carrier 触发。
  usb_serial_jtag_ll_ena_intr_mask(RX_INTR_MASK);
  SetIrqDomainEnabled(true);

  hw_inited_ = true;
  return ErrorCode::OK;
}

void ESP32CDCJtag::SetIrqDomainEnabled(bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  SetIrqDomainEnabledLocked(enabled);
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void ESP32CDCJtag::SetIrqDomainEnabledLocked(bool enabled) noexcept
{
  const bool masked = !enabled;
  if (irq_domain_masked_ == masked)
  {
    return;
  }

  const bool in_isr = xPortInIsrContext() != pdFALSE;
  const bool success =
      intr_handle_ != nullptr && (enabled ? esp_intr_enable(intr_handle_)
                                          : esp_intr_disable(intr_handle_)) == ESP_OK;
  REQUIRE_FROM_CALLBACK(success, in_isr);
  irq_domain_masked_ = masked;
}

void ESP32CDCJtag::SetOwnedInterruptsEnabled(uint32_t mask, bool enabled) noexcept
{
  portENTER_CRITICAL_SAFE(&irq_domain_lock_);
  if (enabled)
  {
    usb_serial_jtag_ll_ena_intr_mask(mask);
  }
  else
  {
    usb_serial_jtag_ll_disable_intr_mask(mask);
  }
  portEXIT_CRITICAL_SAFE(&irq_domain_lock_);
}

void ESP32CDCJtag::ArmTxEmptyInterrupt()
{
  if (tx_empty_interrupt_armed_)
  {
    return;
  }
  tx_empty_interrupt_armed_ = true;
  SetOwnedInterruptsEnabled(TX_INTR_MASK, true);
}

void ESP32CDCJtag::DisarmTxEmptyInterrupt()
{
  if (!tx_empty_interrupt_armed_)
  {
    return;
  }
  SetOwnedInterruptsEnabled(TX_INTR_MASK, false);
  usb_serial_jtag_ll_clr_intsts_mask(TX_INTR_MASK);
  tx_empty_interrupt_armed_ = false;
}

void ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* self = static_cast<ESP32CDCJtag*>(arg);
  if (self != nullptr)
  {
    self->HandleInterrupt();
  }
}

ErrorCode ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* self = LibXR::ContainerOf(&port, &ESP32CDCJtag::_write_port);
  return self->SubmitWrite(in_isr);
}

ErrorCode ESP32CDCJtag::SubmitWrite(bool in_isr)
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

uint32_t ESP32CDCJtag::ServiceIrqSource(bool) noexcept
{
  const uint32_t status = usb_serial_jtag_ll_get_intsts_mask();
  uint32_t events = 0U;
  if ((status & RX_INTR_MASK) != 0U)
  {
    SetOwnedInterruptsEnabled(RX_INTR_MASK, false);
    usb_serial_jtag_ll_clr_intsts_mask(RX_INTR_MASK);
    events |= EventMask(Event::RX_DATA);
  }

  if ((status & TX_INTR_MASK) != 0U)
  {
    // SERIAL_IN_EMPTY is level-like while the endpoint is writable. Make this IRQ a
    // one-shot event carrier; the owner decides whether another empty event is needed.
    SetOwnedInterruptsEnabled(TX_INTR_MASK, false);
    usb_serial_jtag_ll_clr_intsts_mask(TX_INTR_MASK);
    events |= EventMask(Event::TX_EMPTY);
  }
  return events;
}

uint32_t ESP32CDCJtag::ServiceEvents(uint32_t events, bool in_isr, SubmitContext* submit,
                                     bool& pushed_any) noexcept
{
  uint32_t continuation = 0U;
  if ((events & EventMask(Event::TX_EMPTY)) != 0U)
  {
    tx_empty_interrupt_armed_ = false;
  }

  constexpr uint32_t RX_EVENT_MASK =
      EventMask(Event::RX_DATA) | EventMask(Event::RX_SPACE);
  if ((events & RX_EVENT_MASK) != 0U)
  {
    continuation |= ServiceRx(in_isr, pushed_any);
  }

  if ((events & (EventMask(Event::WRITE) | EventMask(Event::TX_EMPTY))) != 0U)
  {
    ProgressTx(in_isr, submit);
  }
  return continuation;
}

void ESP32CDCJtag::ProgressTx(bool in_isr, SubmitContext* submit)
{
  if (!usb_serial_jtag_ll_txfifo_writable())
  {
    ArmTxEmptyInterrupt();
    return;
  }

  bool wrote_any = false;
  while (usb_serial_jtag_ll_txfifo_writable())
  {
    if (!HasCurrentRecord())
    {
      bool synchronous_submission = false;
      if (!ClaimNextRecord(in_isr, submit, synchronous_submission))
      {
        if (wrote_any)
        {
          usb_serial_jtag_ll_txfifo_flush();
          need_zlp_ = false;
        }
        else if (need_zlp_)
        {
          usb_serial_jtag_ll_txfifo_flush();
          need_zlp_ = false;
        }
        DisarmTxEmptyInterrupt();

        // A completion callback or deferred publisher may have queued work while the
        // previous record was finalized. Recheck after disarming the one-shot source.
        if (_write_port.QueueInfo()->Size() != 0U)
        {
          ArmTxEmptyInterrupt();
        }
        return;
      }

      if (!FillCurrentRecord(in_isr, synchronous_submission, submit, wrote_any))
      {
        if (!usb_serial_jtag_ll_txfifo_writable() && wrote_any)
        {
          need_zlp_ = true;
        }
        ArmTxEmptyInterrupt();
        return;
      }
    }
    else if (!FillCurrentRecord(in_isr, false, nullptr, wrote_any))
    {
      if (!usb_serial_jtag_ll_txfifo_writable() && wrote_any)
      {
        need_zlp_ = true;
      }
      ArmTxEmptyInterrupt();
      return;
    }

    if (!usb_serial_jtag_ll_txfifo_writable())
    {
      // A full endpoint auto-flushes. The next writable turn either continues the byte
      // stream or sends its terminating ZLP if no later record has arrived.
      need_zlp_ = true;
      ArmTxEmptyInterrupt();
      return;
    }
  }
}

bool ESP32CDCJtag::ClaimNextRecord(bool in_isr, SubmitContext* submit,
                                   bool& synchronous_submission)
{
  ASSERT(!HasCurrentRecord());

  WriteInfoBlock info{};
  if (_write_port.QueueInfo()->Peek(info) != ErrorCode::OK)
  {
    return false;
  }

  REQUIRE_FROM_CALLBACK(_write_port.QueueData() != nullptr, in_isr);
  REQUIRE_FROM_CALLBACK(info.data.size_ > 0U, in_isr);
  REQUIRE_FROM_CALLBACK(info.data.size_ <= _write_port.QueueData()->Size(), in_isr);
  synchronous_submission =
      submit != nullptr && !submit->resolved_ && _write_port.QueueInfo()->Size() == 1U;

  auto dequeue = _write_port.BeginDequeue(in_isr);
  const ErrorCode result = dequeue.PopInfo(info);
  REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  current_record_ = info;
  current_record_offset_ = 0U;
  return true;
}

bool ESP32CDCJtag::FillCurrentRecord(bool in_isr, bool synchronous_submission,
                                     SubmitContext* submit, bool& wrote_any)
{
  ASSERT(HasCurrentRecord());
  const size_t remaining = current_record_.data.size_ - current_record_offset_;
  const size_t offered = std::min(remaining, ENDPOINT_SIZE);
  uint8_t chunk[ENDPOINT_SIZE] = {};
  const ErrorCode peek_result = _write_port.QueueData()->PeekBatch(chunk, offered);
  REQUIRE_FROM_CALLBACK(peek_result == ErrorCode::OK, in_isr);

  const int written =
      usb_serial_jtag_ll_write_txfifo(chunk, static_cast<uint32_t>(offered));
  REQUIRE_FROM_CALLBACK(written >= 0, in_isr);
  REQUIRE_FROM_CALLBACK(static_cast<size_t>(written) <= offered, in_isr);
  if (written <= 0)
  {
    return false;
  }

  const size_t accepted = static_cast<size_t>(written);
  {
    auto dequeue = _write_port.BeginDequeue(in_isr);
    const ErrorCode pop_result = dequeue.DiscardData(accepted);
    REQUIRE_FROM_CALLBACK(pop_result == ErrorCode::OK, in_isr);
    current_record_offset_ += accepted;
    wrote_any = true;
    need_zlp_ = false;
  }

  if (current_record_offset_ < current_record_.data.size_)
  {
    return false;
  }

  CompleteCurrentRecord(in_isr, synchronous_submission, submit);
  return true;
}

void ESP32CDCJtag::CompleteCurrentRecord(bool in_isr, bool synchronous_submission,
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

bool ESP32CDCJtag::HasCurrentRecord() const { return current_record_.data.size_ != 0U; }

void ESP32CDCJtag::ClearCurrentRecord()
{
  current_record_ = {};
  current_record_offset_ = 0U;
}

void ESP32CDCJtag::ResumeRx(bool in_isr)
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

uint32_t ESP32CDCJtag::ServiceRx(bool in_isr, bool& pushed_any)
{
  SetOwnedInterruptsEnabled(RX_INTR_MASK, false);
  pushed_any = DrainRxToQueue(in_isr) || pushed_any;

  if (_read_port.queue_data_->EmptySize() == 0U)
  {
    return 0U;
  }

  SetOwnedInterruptsEnabled(RX_INTR_MASK, true);
  if (usb_serial_jtag_ll_rxfifo_data_available())
  {
    return EventMask(Event::RX_DATA);
  }
  return 0U;
}

bool ESP32CDCJtag::PushRxBytes(const uint8_t* data, size_t size, bool in_isr)
{
  const ErrorCode result = _read_port.queue_data_->PushBatch(data, size);
  REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  return result == ErrorCode::OK;
}

bool ESP32CDCJtag::DrainRxToQueue(bool in_isr)
{
  bool pushed_any = false;

  while (usb_serial_jtag_ll_rxfifo_data_available())
  {
    const size_t free_space = _read_port.queue_data_->EmptySize();
    if (free_space == 0U)
    {
      break;
    }

    uint8_t rx_tmp[ENDPOINT_SIZE] = {};
    const size_t read_size = std::min(free_space, sizeof(rx_tmp));
    const int got =
        usb_serial_jtag_ll_read_rxfifo(rx_tmp, static_cast<uint32_t>(read_size));
    if (got <= 0)
    {
      break;
    }

    pushed_any = PushRxBytes(rx_tmp, static_cast<size_t>(got), in_isr) || pushed_any;
    if (_read_port.queue_data_->EmptySize() == 0U)
    {
      break;
    }
  }
  return pushed_any;
}

void ESP32CDCJtag::HandleInterrupt()
{
  bool pushed_any = false;
  (void)execution_policy_.InvokeIrq(
      [this]() noexcept { return ServiceIrqSource(true); },
      [this, &pushed_any](uint32_t events) noexcept
      { return ServiceEvents(events, true, nullptr, pushed_any); });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(true);
  }
}

}  // namespace LibXR

#endif
