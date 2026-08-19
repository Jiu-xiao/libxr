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
  REQUIRE(SetConfig(config) == ErrorCode::OK);
  REQUIRE(InitHardware() == ErrorCode::OK);
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config, bool in_isr)
{
  static_cast<void>(in_isr);
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

void ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* self = LibXR::ContainerOf(&port, &ESP32CDCJtag::_write_port);
  auto queue = self->_read_port.GetReadQueue(in_isr);
  (void)self->execution_policy_.Invoke(
      EventMask(Event::WRITE), [self, in_isr, &queue](uint32_t events) noexcept
      { return self->ServiceEvents(events, in_isr, queue); });
  queue.Publish();
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
                                     ReadPort::ReadQueue& queue) noexcept
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
    continuation |= ServiceRx(in_isr, queue);
  }

  if ((events & (EventMask(Event::WRITE) | EventMask(Event::TX_EMPTY))) != 0U)
  {
    ProgressTx(in_isr);
  }
  return continuation;
}

void ESP32CDCJtag::ProgressTx(bool in_isr)
{
  if (!usb_serial_jtag_ll_txfifo_writable())
  {
    ArmTxEmptyInterrupt();
    return;
  }

  size_t snapshot_size = 0U;
  size_t accepted = 0U;
  {
    auto queue = _write_port.GetWriteQueue(in_isr);
    snapshot_size = queue.front_size + queue.next_size;
    if (snapshot_size != 0U)
    {
      accepted = FillTxFifo(queue, in_isr);
    }
  }

  if (snapshot_size == 0U)
  {
    DisarmTxEmptyInterrupt();
    if (tx_flush_pending_)
    {
      (void)_write_port.TryRunWhenWriteQueueIdle(
          [this]()
          {
            usb_serial_jtag_ll_txfifo_flush();
            tx_flush_pending_ = false;
          },
          in_isr);
    }
    return;
  }

  if (accepted < snapshot_size)
  {
    ArmTxEmptyInterrupt();
    return;
  }

  // A third request is intentionally outside this owner scope. Arm its carrier first;
  // stable idle then flushes and disarms only when no such request was published.
  ArmTxEmptyInterrupt();
  (void)_write_port.TryRunWhenWriteQueueIdle(
      [this]()
      {
        usb_serial_jtag_ll_txfifo_flush();
        tx_flush_pending_ = false;
        DisarmTxEmptyInterrupt();
      },
      in_isr);
}

size_t ESP32CDCJtag::FillTxFifo(WritePort::WriteQueue& queue, bool in_isr)
{
  return queue.PopWithWriter(
      ENDPOINT_SIZE,
      [this, in_isr](const uint8_t* first, size_t first_size, const uint8_t* second,
                     size_t second_size) -> size_t
      {
        auto write_span = [this, in_isr](const uint8_t* buffer, size_t size) -> size_t
        {
          if (size == 0U)
          {
            return 0U;
          }
          const int written =
              usb_serial_jtag_ll_write_txfifo(buffer, static_cast<uint32_t>(size));
          REQUIRE_FROM_CALLBACK(written >= 0, in_isr);
          REQUIRE_FROM_CALLBACK(static_cast<size_t>(written) <= size, in_isr);
          return written > 0 ? static_cast<size_t>(written) : 0U;
        };

        const size_t first_written = write_span(first, first_size);
        if (first_written != first_size)
        {
          tx_flush_pending_ = tx_flush_pending_ || first_written != 0U;
          return first_written;
        }

        const size_t second_written = write_span(second, second_size);
        const size_t accepted = first_written + second_written;
        tx_flush_pending_ = tx_flush_pending_ || accepted != 0U;
        return accepted;
      });
}

void ESP32CDCJtag::ResumeRx(bool in_isr)
{
  auto queue = _read_port.GetReadQueue(in_isr);
  (void)execution_policy_.Invoke(EventMask(Event::RX_SPACE),
                                 [this, in_isr, &queue](uint32_t events) noexcept
                                 { return ServiceEvents(events, in_isr, queue); });
  queue.Publish();
}

uint32_t ESP32CDCJtag::ServiceRx(bool in_isr, ReadPort::ReadQueue& queue)
{
  SetOwnedInterruptsEnabled(RX_INTR_MASK, false);
  DrainRxToQueue(queue, in_isr);

  if (queue.EmptySize() == 0U)
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

void ESP32CDCJtag::PushRxBytes(ReadPort::ReadQueue& queue, const uint8_t* data,
                               size_t size, bool in_isr)
{
  const ErrorCode result = queue.PushBatch(data, size);
  REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
}

void ESP32CDCJtag::DrainRxToQueue(ReadPort::ReadQueue& queue, bool in_isr)
{
  while (usb_serial_jtag_ll_rxfifo_data_available())
  {
    const size_t free_space = queue.EmptySize();
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

    PushRxBytes(queue, rx_tmp, static_cast<size_t>(got), in_isr);
    if (queue.EmptySize() == 0U)
    {
      break;
    }
  }
}

void ESP32CDCJtag::HandleInterrupt()
{
  auto queue = _read_port.GetReadQueue(true);
  (void)execution_policy_.InvokeIrq([this]() noexcept { return ServiceIrqSource(true); },
                                    [this, &queue](uint32_t events) noexcept
                                    { return ServiceEvents(events, true, queue); });
  queue.Publish();
}

}  // namespace LibXR

#endif
