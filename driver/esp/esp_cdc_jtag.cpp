#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>
#include <cstring>
#include <new>

#include "esp_attr.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
constexpr uint32_t TX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
constexpr uint32_t RX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;
constexpr uint32_t ALL_INTR_MASK = TX_INTR_MASK | RX_INTR_MASK;
}  // namespace

namespace LibXR
{

ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      config_(config),
      tx_slot_storage_(new (std::nothrow) uint8_t[tx_buffer_size * 2U]),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(tx_slot_storage_ != nullptr);
  ASSERT(tx_buffer_size > 0U);

  tx_double_buffer_.Init({tx_slot_storage_, tx_buffer_size * 2U});

  _read_port = ReadFun;
  _write_port = WriteFun;

  if (SetConfig(config_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  if (InitHardware() != ErrorCode::OK)
  {
    ASSERT(false);
  }
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

  ResetTxState(false);

  const esp_err_t intr_ans = esp_intr_alloc(
      ETS_USB_SERIAL_JTAG_INTR_SOURCE, ESP_INTR_FLAG_IRAM, IsrEntry, this, &intr_handle_);
  intr_installed_ = (intr_ans == ESP_OK);
  if (!intr_installed_)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }

  usb_serial_jtag_ll_clr_intsts_mask(ALL_INTR_MASK);
  usb_serial_jtag_ll_ena_intr_mask(RX_INTR_MASK);

  hw_inited_ = true;
  return ErrorCode::OK;
}

void IRAM_ATTR ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* cdc = static_cast<ESP32CDCJtag*>(arg);
  if (cdc != nullptr)
  {
    cdc->HandleInterrupt();
  }
}

ErrorCode IRAM_ATTR ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = LibXR::ContainerOf(&port, &ESP32CDCJtag::_write_port);
  return cdc->TryStartTx(in_isr);
}

ErrorCode ESP32CDCJtag::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

void IRAM_ATTR ESP32CDCJtag::ClearActiveTx()
{
  tx_double_buffer_.ClearActive();
}

void IRAM_ATTR ESP32CDCJtag::ClearPendingTx()
{
  tx_double_buffer_.ClearPending();
}

void IRAM_ATTR ESP32CDCJtag::ResetTxState(bool)
{
  tx_double_buffer_.Reset();
  tx_busy_.Clear();
}

bool IRAM_ATTR ESP32CDCJtag::DequeueTxToSlot(uint8_t* slot, size_t& size,
                                             WriteInfoBlock& info, bool in_isr)
{
  (void)in_isr;

  WriteInfoBlock peek_info = {};
  if (write_port_->queue_info_->Peek(peek_info) != ErrorCode::OK)
  {
    return false;
  }

  if (peek_info.data.size_ > tx_double_buffer_.BufferSize())
  {
    ASSERT(false);
    return false;
  }

  if (write_port_->queue_data_->PopBatch(slot, peek_info.data.size_) != ErrorCode::OK)
  {
    return false;
  }

  if (write_port_->queue_info_->Pop(info) != ErrorCode::OK)
  {
    return false;
  }

  size = peek_info.data.size_;
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::LoadActiveTxFromQueue(bool in_isr)
{
  if (tx_double_buffer_.HasActive())
  {
    return true;
  }

  size_t active_size = 0U;
  WriteInfoBlock active_info = {};
  if (!DequeueTxToSlot(tx_double_buffer_.ActiveBuffer(), active_size, active_info,
                       in_isr))
  {
    return false;
  }

  tx_double_buffer_.LoadActive(active_size, active_info);
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::LoadPendingTxFromQueue(bool in_isr)
{
  if (tx_double_buffer_.HasPending())
  {
    return false;
  }

  size_t pending_size = 0U;
  WriteInfoBlock pending_info = {};
  if (!DequeueTxToSlot(tx_double_buffer_.PendingBuffer(), pending_size, pending_info,
                       in_isr))
  {
    return false;
  }

  tx_double_buffer_.LoadPending(pending_size, pending_info);
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::StartPendingTxIfIdle(bool in_isr)
{
  if (tx_busy_.IsSet() || tx_double_buffer_.HasActive() ||
      !tx_double_buffer_.HasPending())
  {
    return false;
  }

  if (!tx_double_buffer_.PromotePending())
  {
    return false;
  }

  return StartAndReportActive(in_isr);
}

bool IRAM_ATTR ESP32CDCJtag::PumpTx(bool)
{
  while (tx_busy_.IsSet())
  {
    if (!tx_double_buffer_.HasActive() || (tx_double_buffer_.ActiveBuffer() == nullptr) ||
        (tx_double_buffer_.ActiveOffset() >= tx_double_buffer_.ActiveLength()))
    {
      tx_busy_.Clear();
      return true;
    }

    const size_t active_offset = tx_double_buffer_.ActiveOffset();
    const uint32_t remain =
        static_cast<uint32_t>(tx_double_buffer_.ActiveLength() - active_offset);
    const int written = usb_serial_jtag_ll_write_txfifo(
        tx_double_buffer_.ActiveBuffer() + active_offset, remain);
    if (written <= 0)
    {
      return false;
    }

    tx_double_buffer_.SetActiveOffset(active_offset + static_cast<size_t>(written));
    if (tx_double_buffer_.ActiveOffset() >= tx_double_buffer_.ActiveLength())
    {
      tx_busy_.Clear();
      return true;
    }
  }

  return false;
}

void IRAM_ATTR ESP32CDCJtag::PushRxBytes(const uint8_t* data, size_t size, bool in_isr)
{
  size_t offset = 0U;
  bool pushed_any = false;

  while (offset < size)
  {
    const size_t free_space = read_port_->queue_data_->EmptySize();
    if (free_space == 0U)
    {
      break;
    }

    const size_t chunk = std::min(free_space, size - offset);
    if (read_port_->queue_data_->PushBatch(data + offset, chunk) != ErrorCode::OK)
    {
      break;
    }

    offset += chunk;
    pushed_any = true;
  }

  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }
}

bool IRAM_ATTR ESP32CDCJtag::StartActiveTransfer(bool in_isr)
{
  if (!tx_double_buffer_.HasActive() || (tx_double_buffer_.ActiveBuffer() == nullptr) ||
      (tx_double_buffer_.ActiveLength() == 0U))
  {
    return false;
  }

  if (tx_busy_.TestAndSet())
  {
    return true;
  }

  tx_double_buffer_.SetActiveOffset(0U);
  usb_serial_jtag_ll_ena_intr_mask(TX_INTR_MASK);
  (void)PumpTx(in_isr);
  return true;
}

bool IRAM_ATTR ESP32CDCJtag::StartAndReportActive(bool in_isr)
{
  if (!StartActiveTransfer(in_isr))
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_double_buffer_.ActiveInfo());
    ClearActiveTx();
    return false;
  }

  // Keep aligned with STM/CH: once next op is kicked to HW, report it finished.
  write_port_->Finish(in_isr, ErrorCode::OK, tx_double_buffer_.ActiveInfo());
  if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
  {
    OnTxTransferDone(in_isr, ErrorCode::OK);
  }
  return true;
}

void IRAM_ATTR ESP32CDCJtag::StopTxTransfer()
{
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_disable_intr_mask(TX_INTR_MASK);
}

void IRAM_ATTR ESP32CDCJtag::OnTxTransferDone(bool in_isr, ErrorCode result)
{
  Flag::ScopedRestore tx_flag(in_tx_isr_);
  tx_busy_.Clear();

  ClearActiveTx();

  if ((result != ErrorCode::OK) && tx_double_buffer_.HasPending())
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_double_buffer_.PendingInfo());
    ClearPendingTx();
  }

  if (result != ErrorCode::OK)
  {
    StopTxTransfer();
    return;
  }

  if (StartPendingTxIfIdle(in_isr))
  {
    if (!tx_double_buffer_.HasPending())
    {
      (void)LoadPendingTxFromQueue(in_isr);
      (void)StartPendingTxIfIdle(in_isr);
    }
  }

  if (!tx_busy_.IsSet() && !tx_double_buffer_.HasActive() &&
      !tx_double_buffer_.HasPending())
  {
    StopTxTransfer();
  }
}

ErrorCode IRAM_ATTR ESP32CDCJtag::TryStartTx(bool in_isr)
{
  if (in_tx_isr_.IsSet())
  {
    return ErrorCode::PENDING;
  }

  if (StartPendingTxIfIdle(in_isr))
  {
    return ErrorCode::PENDING;
  }

  if (!tx_double_buffer_.HasActive())
  {
    (void)LoadActiveTxFromQueue(in_isr);
  }

  if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
  {
    if (!StartActiveTransfer(in_isr))
    {
      ClearActiveTx();
      return ErrorCode::FAILED;
    }

    if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
    {
      OnTxTransferDone(in_isr, ErrorCode::OK);
    }

    return ErrorCode::OK;
  }

  if (!tx_double_buffer_.HasPending())
  {
    (void)LoadPendingTxFromQueue(in_isr);
    (void)StartPendingTxIfIdle(in_isr);
  }

  return ErrorCode::PENDING;
}

void IRAM_ATTR ESP32CDCJtag::HandleInterrupt()
{
  const uint32_t status = usb_serial_jtag_ll_get_intsts_mask();

  const uint32_t rx_status = status & RX_INTR_MASK;
  if (rx_status != 0U)
  {
    usb_serial_jtag_ll_clr_intsts_mask(rx_status);

    uint8_t rx_tmp[64] = {};
    const int got = usb_serial_jtag_ll_read_rxfifo(rx_tmp, sizeof(rx_tmp));
    if (got > 0)
    {
      PushRxBytes(rx_tmp, static_cast<size_t>(got), true);
    }
  }

  const uint32_t tx_status = status & TX_INTR_MASK;
  if (tx_status == 0U)
  {
    return;
  }

  usb_serial_jtag_ll_clr_intsts_mask(tx_status);

  Flag::ScopedRestore tx_flag(in_tx_isr_);
  const bool was_busy = tx_busy_.IsSet();
  (void)PumpTx(true);
  if (was_busy && !tx_busy_.IsSet())
  {
    OnTxTransferDone(true, ErrorCode::OK);
  }
}

}  // namespace LibXR

#endif
