#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>

#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
constexpr uint32_t TX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
constexpr uint32_t RX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;
constexpr uint32_t ALL_INTR_MASK = TX_INTR_MASK | RX_INTR_MASK;
constexpr size_t ENDPOINT_SIZE = 64U;

constexpr bool IsConfigSupported(const LibXR::UART::Configuration& config)
{
  return (config.data_bits == 8U) && (config.stop_bits == 1U) &&
         (config.parity == LibXR::UART::Parity::NO_PARITY);
}
}  // namespace

namespace LibXR
{

void ESP32CDCJtagReadPort::OnReadQueueSpaceAvailable(bool in_isr)
{
  owner_.ResumeRx(in_isr);
}

ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      _read_port(rx_buffer_size, *this),
      _write_port(tx_queue_size, tx_buffer_size)
{
  REQUIRE(rx_buffer_size > 0U);
  REQUIRE(tx_buffer_size > 0U);
  REQUIRE(tx_queue_size > 0U);

  INIT_CRIT_SECTION_LOCK_RUNTIME(&irq_lock_);

  REQUIRE(IsConfigSupported(config));
  REQUIRE(InitHardware() == ErrorCode::OK);

  _write_port = WriteFun;
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config, bool)
{
  return IsConfigSupported(config) ? ErrorCode::OK : ErrorCode::ARG_ERR;
}

ErrorCode ESP32CDCJtag::InitHardware()
{
  if (hw_inited_)
  {
    return ErrorCode::OK;
  }

  const esp_err_t intr_ans =
      esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 0, IsrEntry, this, &intr_handle_);
  if (intr_ans != ESP_OK)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }

  DisableAndClearInterrupt(ALL_INTR_MASK);
  EnableInterrupt(RX_INTR_MASK);

  hw_inited_ = true;
  return ErrorCode::OK;
}

void ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* cdc = static_cast<ESP32CDCJtag*>(arg);
  if (cdc != nullptr)
  {
    cdc->HandleInterrupt();
  }
}

void ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = LibXR::ContainerOf(&port, &ESP32CDCJtag::_write_port);
  cdc->service_.Invoke(EVENT_WRITE, in_isr, [cdc](uint32_t events, bool owner_isr)
                       { cdc->ServiceEvents(events, owner_isr); });
}

void ESP32CDCJtag::ResumeRx(bool in_isr)
{
  service_.Invoke(EVENT_RX_SPACE, in_isr, [this](uint32_t events, bool owner_isr)
                  { ServiceEvents(events, owner_isr); });
}

void ESP32CDCJtag::ServiceEvents(uint32_t events, bool in_isr)
{
  if ((events & EVENT_TX_EMPTY) != 0U)
  {
    tx_empty_interrupt_armed_ = false;
  }
  if ((events & (EVENT_RX_DATA | EVENT_RX_SPACE)) != 0U)
  {
    auto queue = _read_port.GetReadQueue(in_isr);
    ServiceRx(queue, in_isr);
    queue.Publish();
  }

  if ((events & (EVENT_WRITE | EVENT_TX_EMPTY)) != 0U)
  {
    ProgressTx(in_isr);
  }
}

void ESP32CDCJtag::PushRxBytes(ReadPort::ReadQueue& queue, const uint8_t* data,
                               size_t size, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(queue.PushBatch(data, size) == ErrorCode::OK, in_isr);
}

void ESP32CDCJtag::DrainRxToQueue(ReadPort::ReadQueue& queue, bool in_isr)
{
  while (usb_serial_jtag_ll_rxfifo_data_available())
  {
    const size_t free_space = queue.EmptySize();
    if (free_space == 0U)
    {
      return;
    }

    uint8_t rx_tmp[ENDPOINT_SIZE] = {};
    const size_t read_size = std::min(free_space, sizeof(rx_tmp));
    const int got =
        usb_serial_jtag_ll_read_rxfifo(rx_tmp, static_cast<uint32_t>(read_size));
    if (got <= 0)
    {
      return;
    }
    REQUIRE_FROM_CALLBACK(static_cast<size_t>(got) <= read_size, in_isr);
    PushRxBytes(queue, rx_tmp, static_cast<size_t>(got), in_isr);
  }
}

void ESP32CDCJtag::ServiceRx(ReadPort::ReadQueue& queue, bool in_isr)
{
  DisableInterrupt(RX_INTR_MASK);
  DrainRxToQueue(queue, in_isr);

  if (queue.EmptySize() != 0U)
  {
    EnableInterrupt(RX_INTR_MASK);
  }
}

void ESP32CDCJtag::ArmTxEmptyInterrupt()
{
  if (!tx_empty_interrupt_armed_)
  {
    EnableInterrupt(TX_INTR_MASK);
    tx_empty_interrupt_armed_ = true;
  }
}

void ESP32CDCJtag::DisarmTxEmptyInterrupt()
{
  if (tx_empty_interrupt_armed_)
  {
    DisableAndClearInterrupt(TX_INTR_MASK);
    tx_empty_interrupt_armed_ = false;
  }
  else
  {
    ClearInterrupt(TX_INTR_MASK);
  }
}

void ESP32CDCJtag::FlushTxFifo()
{
  if (tx_flush_pending_)
  {
    usb_serial_jtag_ll_txfifo_flush();
    tx_flush_pending_ = false;
  }
}

size_t ESP32CDCJtag::FillTxFifo(WritePort::WriteQueue& queue, bool in_isr)
{
  return queue.PopWithWriter(
      ENDPOINT_SIZE,
      [this, in_isr](const uint8_t* first, size_t first_size, const uint8_t* second,
                     size_t second_size) -> size_t
      {
        auto write_span = [this, in_isr](const uint8_t* data, size_t size) -> size_t
        {
          if (size == 0U)
          {
            return 0U;
          }

          const int written =
              usb_serial_jtag_ll_write_txfifo(data, static_cast<uint32_t>(size));
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

void ESP32CDCJtag::ProgressTx(bool in_isr)
{
  if (!usb_serial_jtag_ll_txfifo_writable())
  {
    ArmTxEmptyInterrupt();
    return;
  }

  size_t accepted = 0U;
  {
    auto queue = _write_port.GetWriteQueue(in_isr);
    if (queue.Empty())
    {
      FlushTxFifo();
      DisarmTxEmptyInterrupt();
      return;
    }

    accepted = FillTxFifo(queue, in_isr);
  }

  if (accepted == 0U)
  {
    ArmTxEmptyInterrupt();
    return;
  }

  if (_write_port.Size() == 0U)
  {
    if (usb_serial_jtag_ll_txfifo_writable())
    {
      FlushTxFifo();
      DisarmTxEmptyInterrupt();
    }
    else
    {
      // 满包需要等下一次 FIFO 可写后再发送结束用的 ZLP。
      // A full packet needs a later writable turn for its terminating ZLP.
      ArmTxEmptyInterrupt();
    }
  }
  else
  {
    if (usb_serial_jtag_ll_txfifo_writable())
    {
      FlushTxFifo();
    }
    ArmTxEmptyInterrupt();
  }
}

void ESP32CDCJtag::HandleInterrupt()
{
  uint32_t status = CaptureInterrupt(ALL_INTR_MASK);
  while (true)
  {
    const uint32_t handled = status & ALL_INTR_MASK;
    if (handled == 0U)
    {
      return;
    }

    uint32_t events = 0U;
    if ((handled & RX_INTR_MASK) != 0U)
    {
      events |= EVENT_RX_DATA;
    }
    if ((handled & TX_INTR_MASK) != 0U)
    {
      events |= EVENT_TX_EMPTY;
    }

    service_.Invoke(events, true, [this](uint32_t owner_events, bool owner_isr)
                    { ServiceEvents(owner_events, owner_isr); });

    status = CaptureInterrupt(ALL_INTR_MASK);
  }
}

void ESP32CDCJtag::EnableInterrupt(uint32_t mask)
{
  esp_os_enter_critical_safe(&irq_lock_);
  usb_serial_jtag_ll_ena_intr_mask(mask);
  esp_os_exit_critical_safe(&irq_lock_);
}

void ESP32CDCJtag::DisableInterrupt(uint32_t mask)
{
  esp_os_enter_critical_safe(&irq_lock_);
  usb_serial_jtag_ll_disable_intr_mask(mask);
  esp_os_exit_critical_safe(&irq_lock_);
}

void ESP32CDCJtag::ClearInterrupt(uint32_t mask)
{
  esp_os_enter_critical_safe(&irq_lock_);
  usb_serial_jtag_ll_clr_intsts_mask(mask);
  esp_os_exit_critical_safe(&irq_lock_);
}

void ESP32CDCJtag::DisableAndClearInterrupt(uint32_t mask)
{
  esp_os_enter_critical_safe(&irq_lock_);
  usb_serial_jtag_ll_disable_intr_mask(mask);
  usb_serial_jtag_ll_clr_intsts_mask(mask);
  esp_os_exit_critical_safe(&irq_lock_);
}

uint32_t ESP32CDCJtag::CaptureInterrupt(uint32_t mask)
{
  esp_os_enter_critical_safe(&irq_lock_);
  const uint32_t handled = usb_serial_jtag_ll_get_intsts_mask() & mask;
  if (handled != 0U)
  {
    usb_serial_jtag_ll_disable_intr_mask(handled);
    usb_serial_jtag_ll_clr_intsts_mask(handled);
  }
  esp_os_exit_critical_safe(&irq_lock_);
  return handled;
}

}  // namespace LibXR

#endif
