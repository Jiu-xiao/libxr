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
      execution_policy_(*this),
      _read_port(rx_buffer_size, *this),
      _write_port(tx_queue_size, tx_buffer_size),
      tx_model_(_write_port)
{
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_buffer_size > 0U);
  REQUIRE(tx_queue_size > 0U);
  if constexpr (Detail::ESP_UART_USES_IRQ_SERIALIZATION)
  {
    REQUIRE(Detail::IsCurrentTaskPinnedToOneCore());
  }

  _write_port = WriteFun;
  REQUIRE(SetConfig(config) == ErrorCode::OK);
  REQUIRE(InitHardware() == ErrorCode::OK);
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config)
{
  if ((config.data_bits != 8) || (config.stop_bits != 1) ||
      (config.parity != UART::Parity::NO_PARITY))
  {
    return ErrorCode::ARG_ERR;
  }
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
  bool pushed_any = false;
  (void)execution_policy_.Invoke(EventMask(Event::WRITE),
                                 [this, in_isr, &pushed_any](uint32_t events) noexcept
                                 { return ServiceEvents(events, in_isr, pushed_any); });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(in_isr);
  }
  return ErrorCode::PENDING;
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

uint32_t ESP32CDCJtag::ServiceEvents(uint32_t events, bool in_isr,
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
    ProgressTx(in_isr);
  }
  return continuation;
}

void ESP32CDCJtag::ProgressTx(bool in_isr)
{
  while (true)
  {
    if (tx_model_.HasPendingCompletion())
    {
      if (!tx_model_.PublishPendingCompletion(in_isr, TxCompletionPublication::ALLOW))
      {
        return;
      }
      continue;
    }

    if (!usb_serial_jtag_ll_txfifo_writable())
    {
      ArmTxEmptyInterrupt();
      return;
    }

    if (!tx_model_.HasActiveRecord())
    {
      if (!tx_model_.TryClaim(in_isr))
      {
        DisarmTxEmptyInterrupt();

        if (!tx_flush_pending_)
        {
          return;
        }

        // Avoid closing a packet while metadata publication is already in progress.
        if (!_write_port.TryPublishBackendCompletion())
        {
          return;
        }
        if (_write_port.QueueInfo()->Size() != 0U)
        {
          continue;
        }
        usb_serial_jtag_ll_txfifo_flush();
        tx_flush_pending_ = false;
        return;
      }
    }

    if (!FillTxFifo(in_isr))
    {
      if (tx_model_.HasActiveRecord())
      {
        ArmTxEmptyInterrupt();
      }
      return;
    }
  }
}

bool ESP32CDCJtag::FillTxFifo(bool in_isr)
{
  ASSERT(tx_model_.HasActiveRecord());
  uint8_t chunk[ENDPOINT_SIZE] = {};
  return tx_model_.FillWithScratch(
      in_isr, ENDPOINT_SIZE, RawData{chunk, sizeof(chunk)},
      TxCompletionPublication::ALLOW,
      [this, in_isr](const uint8_t* buffer, size_t size) -> size_t
      {
        const int written =
            usb_serial_jtag_ll_write_txfifo(buffer, static_cast<uint32_t>(size));
        REQUIRE_FROM_CALLBACK(written >= 0, in_isr);
        REQUIRE_FROM_CALLBACK(static_cast<size_t>(written) <= size, in_isr);
        if (written <= 0)
        {
          return 0U;
        }

        // Preserve packet work until the model and metadata queue are truly quiescent.
        // One flush sends a short packet or terminates an exact 64-byte packet with ZLP.
        tx_flush_pending_ = true;
        return static_cast<size_t>(written);
      });
}

void ESP32CDCJtag::ResumeRx(bool in_isr)
{
  bool pushed_any = false;
  (void)execution_policy_.Invoke(EventMask(Event::RX_SPACE),
                                 [this, in_isr, &pushed_any](uint32_t events) noexcept
                                 { return ServiceEvents(events, in_isr, pushed_any); });
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
  (void)execution_policy_.InvokeIrq([this]() noexcept { return ServiceIrqSource(true); },
                                    [this, &pushed_any](uint32_t events) noexcept
                                    { return ServiceEvents(events, true, pushed_any); });
  if (pushed_any)
  {
    _read_port.ProcessPendingReads(true);
  }
}

}  // namespace LibXR

#endif
